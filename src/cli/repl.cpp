// minedict - BSD 3-Clause License; see README.
#include "minedict/cli/repl.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "minedict/cli/compat.hpp"
#include "minedict/cli/options.hpp"
#include "minedict/cli/terminal.hpp"
#include "minedict/cli/text.hpp"

#ifndef MINEDICT_HAVE_READLINE
#define MINEDICT_HAVE_READLINE 0
#endif
#ifndef MINEDICT_READLINE_IS_LIBEDIT
#define MINEDICT_READLINE_IS_LIBEDIT 0
#endif
#ifndef MINEDICT_READLINE_HEADER_EDITLINE
#define MINEDICT_READLINE_HEADER_EDITLINE 0
#endif

#if MINEDICT_HAVE_READLINE
#if MINEDICT_READLINE_HEADER_EDITLINE
#include <editline/readline.h>
#else
#include <readline/history.h>
#include <readline/readline.h>
#endif
#endif

namespace minedict::cli {
namespace {

using Completer = std::function<std::vector<std::string>(std::string_view)>;

#if MINEDICT_HAVE_READLINE
// readline talks to us through plain function pointers, hence the globals.
Completer* g_completer = nullptr;
std::vector<std::string> g_matches;
std::size_t g_match_index = 0;
char g_no_break_characters[] = "";

char* match_generator(const char* text, int state) {
  if (state == 0) {
    g_matches.clear();
    g_match_index = 0;
    if (g_completer != nullptr) {
      std::string_view line{text};
      std::string prefix;
      if (line.starts_with('/')) {  // complete the argument of a slash command
        const std::size_t space = line.find(' ');
        if (space == std::string_view::npos) return nullptr;
        prefix = std::string(line.substr(0, space + 1));
        line = line.substr(space + 1);
      }
      for (std::string& match : (*g_completer)(line)) g_matches.push_back(prefix + match);
    }
  }
  if (g_match_index >= g_matches.size()) return nullptr;
  return ::strdup(g_matches[g_match_index++].c_str());
}

char** completion_hook(const char* text, int /*start*/, int /*end*/) {
  rl_attempted_completion_over = 1;  // never fall back to filename completion
  return rl_completion_matches(text, match_generator);
}

/// Wrap escape sequences in RL_PROMPT_START_IGNORE/END_IGNORE so readline
/// measures the prompt correctly.
std::string readline_safe(std::string_view styled) {
  std::string out;
  for (std::size_t i = 0; i < styled.size();) {
    if (styled[i] == '\x1b') {
      std::size_t j = i + 1;
      if (j < styled.size() && styled[j] == '[') {
        ++j;
        while (j < styled.size() && !(styled[j] >= 0x40 && styled[j] <= 0x7E)) ++j;
        if (j < styled.size()) ++j;
      }
      out += '\001';
      out.append(styled.substr(i, j - i));
      out += '\002';
      i = j;
      continue;
    }
    out += styled[i++];
  }
  return out;
}
#endif

class LineEditor {
 public:
  LineEditor(std::filesystem::path history_file, Completer completer)
      : history_file_(std::move(history_file)), completer_(std::move(completer)) {
#if MINEDICT_HAVE_READLINE
    rl_readline_name = "minedict";
    rl_attempted_completion_function = completion_hook;
    rl_completer_word_break_characters = g_no_break_characters;
    rl_basic_word_break_characters = g_no_break_characters;
    g_completer = &completer_;
    using_history();
    stifle_history(2000);
    std::error_code ec;
    std::filesystem::create_directories(history_file_.parent_path(), ec);
    read_history(history_file_.string().c_str());
#endif
  }

  ~LineEditor() {
#if MINEDICT_HAVE_READLINE
    write_history(history_file_.string().c_str());
    g_completer = nullptr;
#endif
  }

  LineEditor(const LineEditor&) = delete;
  LineEditor& operator=(const LineEditor&) = delete;

  /// Read one line; nothing on end of input.
  std::optional<std::string> read(std::string_view plain_prompt, std::string_view styled_prompt) {
#if MINEDICT_HAVE_READLINE
#if MINEDICT_READLINE_IS_LIBEDIT
    const std::string prompt(plain_prompt);  // libedit cannot measure styled prompts
#else
    (void)plain_prompt;
    const std::string prompt = readline_safe(styled_prompt);
#endif
    char* raw = ::readline(prompt.c_str());
    if (raw == nullptr) return std::nullopt;
    std::string line(raw);
    std::free(raw);  // NOLINT(cppcoreguidelines-no-malloc)
    return line;
#else
    (void)plain_prompt;
    write(stdout, styled_prompt);
    std::fflush(stdout);
    std::string line;
    if (!std::getline(std::cin, line)) return std::nullopt;
    return line;
#endif
  }

  void remember(std::string_view line) {
#if MINEDICT_HAVE_READLINE
    add_history(std::string(line).c_str());
#else
    (void)line;
#endif
  }

 private:
  std::filesystem::path history_file_;
  Completer completer_;
};

bool all_digits(std::string_view s) noexcept {
  return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

std::optional<std::size_t> parse_number(std::string_view s) noexcept {
  std::size_t value = 0;
  const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
  if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
  return value;
}

constexpr std::size_t kMaxComfortableWidth = 100;

}  // namespace

Repl::Repl(const Shelf& shelf, Theme theme, ColorMode color, Settings settings)
    : shelf_(shelf), theme_(std::move(theme)), presenter_(theme_, color, 80), settings_(settings) {
  presenter_.set_css(settings_.use_css);
  refresh_width();
}

void Repl::refresh_width() {
  if (settings_.fixed_width) {
    presenter_.set_width(*settings_.fixed_width == 0 ? 1'000'000 : *settings_.fixed_width);
    return;
  }
  const auto size = terminal_size();
  presenter_.set_width(size ? std::min(size->columns, kMaxComfortableWidth) : 80);
}

void Repl::say(std::string_view text) const {
  write(stdout, text);
  if (!text.ends_with('\n')) std::fputc('\n', stdout);
  std::fflush(stdout);
}

void Repl::greet() const {
  say(presenter_.paint(std::format("minedict {} · {} {} loaded", version_string(), shelf_.size(),
                                   shelf_.size() == 1 ? "dictionary" : "dictionaries"),
                       Role::muted));
  for (const Dictionary& dictionary : shelf_.dictionaries()) {
    say(std::format("  {} {} {}", presenter_.paint("•", Role::bullet), dictionary.metadata().title,
                    presenter_.paint(std::format("({} entries)", text::with_thousands(dictionary.metadata().entries)),
                                     Role::muted)));
  }
  say(presenter_.paint("type a word and press enter · tab completes headwords · /help for commands",
                       Role::prompt_hint));
}

int Repl::run() {
  greet();
  LineEditor editor{state_directory() / "history",
                    [this](std::string_view prefix) { return shelf_.complete(prefix, 200); }};
  const std::string plain_prompt = "minedict ❯ ";
  while (running_) {
    refresh_width();
    const std::string styled_prompt =
        presenter_.paint("minedict", Role::prompt) + " " + presenter_.paint("❯", Role::prompt) + " ";
    const auto line = editor.read(plain_prompt, styled_prompt);
    if (!line) {
      say("");
      break;
    }
    const std::string_view trimmed = text::trim(*line);
    if (trimmed.empty()) continue;
    editor.remember(trimmed);
    handle(trimmed);
  }
  return 0;
}

void Repl::handle(std::string_view line) {
  const std::string_view trimmed = text::trim(line);
  if (trimmed.empty()) return;
  if (trimmed.starts_with('/')) {
    command(trimmed);
  } else if (trimmed == "exit" || trimmed == "quit") {
    running_ = false;
  } else if (all_digits(trimmed)) {
    if (const auto number = parse_number(trimmed); number && !last_matches_.empty()) {
      pick(*number);
    } else {
      lookup(trimmed, settings_.raw);
    }
  } else {
    lookup(trimmed, settings_.raw);
  }
}

void Repl::lookup(std::string_view word, bool raw) {
  const auto outcome = shelf_.lookup(word, LookupOptions{.follow_links = settings_.follow_links});
  for (const Error& problem : outcome.problems) say(presenter_.warning(problem.message));

  if (outcome.found()) {
    std::string out;
    if (raw) {
      for (const Shelf::Hit& hit : outcome.hits) {
        for (const Entry& entry : hit.entries) {
          out += entry.definition;
          out += '\n';
        }
      }
    } else {
      out = presenter_.entries(outcome, presenter_.renderer(2, settings_.show_images, settings_.show_sounds));
    }
    out += '\n';
    emit(out, settings_.pager);
    last_matches_.clear();
    return;
  }

  // Nothing found: offer numbered alternatives the user can pick by number.
  last_matches_.clear();
  for (const Shelf::Candidate& candidate : shelf_.suggest(word, 8)) {
    last_matches_.push_back(
        Shelf::Match{.dictionary = candidate.dictionary, .key = candidate.dictionary->key_at(candidate.suggestion.position)});
  }
  if (last_matches_.empty()) last_matches_ = shelf_.find_prefix(word, 8);

  std::string out = presenter_.not_found(word, {}, {});
  if (!last_matches_.empty()) {
    out += "  ";
    out += presenter_.paint("did you mean (type the number):", Role::muted);
    out += '\n';
    out += presenter_.matches(word, last_matches_, true, shelf_.size() > 1);
  }
  say(out);
}

void Repl::search(std::string_view pattern, bool contains) {
  last_matches_ = contains ? shelf_.find_containing(pattern, settings_.limit) : shelf_.find_prefix(pattern, settings_.limit);
  if (last_matches_.empty()) {
    say(presenter_.problem(std::format("no headwords {} “{}”", contains ? "containing" : "starting with", pattern)));
    return;
  }
  std::string out = presenter_.matches(pattern, last_matches_, true, shelf_.size() > 1);
  if (settings_.limit != 0 && last_matches_.size() >= settings_.limit) {
    out += presenter_.paint(std::format("  showing the first {} · /limit <n> changes this", settings_.limit), Role::muted);
    out += '\n';
  }
  out += presenter_.paint("  type a number to open an entry", Role::prompt_hint);
  emit(out + "\n", settings_.pager);
}

void Repl::pick(std::size_t number) {
  if (number == 0 || number > last_matches_.size()) {
    say(presenter_.problem(std::format("no result number {} (1-{})", number, last_matches_.size())));
    return;
  }
  const Shelf::Match match = last_matches_[number - 1];
  lookup(match.key.key, settings_.raw);
}

void Repl::command(std::string_view line) {
  const std::size_t space = line.find(' ');
  const std::string_view name = line.substr(0, space);
  const std::string_view argument = space == std::string_view::npos ? std::string_view{} : text::trim(line.substr(space + 1));

  if (name == "/quit" || name == "/q" || name == "/exit") {
    running_ = false;
  } else if (name == "/help" || name == "/h" || name == "/?") {
    show_help();
  } else if (name == "/dicts" || name == "/d") {
    show_dictionaries();
  } else if (name == "/info") {
    show_info(argument.empty() ? std::nullopt : parse_number(argument));
  } else if (name == "/search" || name == "/s") {
    if (argument.empty()) say(presenter_.problem("usage: /search <prefix>"));
    else search(argument, false);
  } else if (name == "/contains" || name == "/c") {
    if (argument.empty()) say(presenter_.problem("usage: /contains <text>"));
    else search(argument, true);
  } else if (name == "/raw" || name == "/html") {
    if (argument.empty()) say(presenter_.problem("usage: /raw <word>"));
    else lookup(argument, true);
  } else if (name == "/limit") {
    if (const auto value = parse_number(argument)) {
      settings_.limit = *value;
      say(presenter_.note(std::format("search results limited to {}", *value == 0 ? "all" : std::to_string(*value))));
    } else {
      say(presenter_.problem("usage: /limit <number> (0 = all)"));
    }
  } else if (name == "/width") {
    if (const auto value = parse_number(argument)) {
      settings_.fixed_width = *value;
      refresh_width();
      say(presenter_.note(*value == 0 ? "wrapping disabled" : std::format("wrapping at {} columns", *value)));
    } else if (argument == "auto") {
      settings_.fixed_width.reset();
      refresh_width();
      say(presenter_.note("wrapping follows the terminal width"));
    } else {
      say(presenter_.problem("usage: /width <columns>|auto (0 = never wrap)"));
    }
  } else if (name == "/theme") {
    if (auto theme = Theme::named(argument)) {
      theme_ = std::move(*theme);
      presenter_ = Presenter{theme_, presenter_.color(), presenter_.width()};
      presenter_.set_css(settings_.use_css);
      say(presenter_.note(std::format("theme: {}", theme_.name())));
    } else {
      say(presenter_.problem("usage: /theme dracula|plain"));
    }
  } else if (name == "/links") {
    settings_.follow_links = !settings_.follow_links;
    say(presenter_.note(settings_.follow_links ? "following @@@LINK redirects" : "showing redirects as stored"));
  } else if (name == "/images") {
    settings_.show_images = !settings_.show_images;
    say(presenter_.note(settings_.show_images ? "image placeholders shown" : "image placeholders hidden"));
  } else if (name == "/sounds") {
    settings_.show_sounds = !settings_.show_sounds;
    say(presenter_.note(settings_.show_sounds ? "sound links marked with ♪" : "sound links hidden"));
  } else if (name == "/css") {
    settings_.use_css = !settings_.use_css;
    presenter_.set_css(settings_.use_css);
    say(presenter_.note(settings_.use_css ? "using each dictionary's stylesheet" : "ignoring dictionary stylesheets"));
  } else if (name == "/pager") {
    settings_.pager = !settings_.pager;
    say(presenter_.note(settings_.pager ? "long entries open in the pager" : "pager disabled"));
  } else {
    say(presenter_.problem(std::format("unknown command {} · /help lists the commands", name)));
  }
}

void Repl::show_help() const {
  const auto row = [this](std::string_view command, std::string_view description) {
    return std::format("  {}  {}\n", presenter_.paint(text::pad_right(command, 22), Role::key), description);
  };
  std::string out;
  out += row("<word>", "look a word up in every dictionary");
  out += row("<number>", "open that result from the last search or suggestion list");
  out += row("/search <prefix>, /s", "list headwords starting with a prefix");
  out += row("/contains <text>, /c", "list headwords containing some text");
  out += row("/raw <word>", "show the stored HTML of an entry");
  out += row("/dicts, /d", "list the loaded dictionaries");
  out += row("/info [n]", "details about one or all dictionaries");
  out += row("/limit <n>", "how many search results to show (0 = all)");
  out += row("/width <n>|auto", "wrap width (0 = never wrap)");
  out += row("/theme dracula|plain", "switch the colour theme");
  out += row("/links, /images, /pager", "toggle redirects, image placeholders, the pager");
  out += row("/css, /sounds", "toggle the dictionary's stylesheet, ♪ sound markers");
  out += row("/quit, /q", "leave (so does Ctrl-D)");
  say(out);
}

void Repl::show_dictionaries() const {
  std::string out;
  std::size_t index = 0;
  for (const Dictionary& dictionary : shelf_.dictionaries()) {
    const Metadata& meta = dictionary.metadata();
    out += std::format("  {}  {} {}\n", presenter_.paint(std::format("{:>2}", ++index), Role::muted), meta.title,
                       presenter_.paint(std::format("· {} · {} entries · {}", to_string(meta.kind),
                                                    text::with_thousands(meta.entries), meta.path.filename().string()),
                                        Role::muted));
  }
  say(out);
}

void Repl::show_info(std::optional<std::size_t> index) const {
  std::string out;
  if (index) {
    if (*index == 0 || *index > shelf_.size()) {
      say(presenter_.problem(std::format("no dictionary number {} (1-{})", *index, shelf_.size())));
      return;
    }
    out = presenter_.info(shelf_.at(*index - 1));
  } else {
    bool first = true;
    for (const Dictionary& dictionary : shelf_.dictionaries()) {
      if (!std::exchange(first, false)) out += '\n';
      out += presenter_.info(dictionary);
    }
  }
  emit(out + "\n", settings_.pager);
}

}  // namespace minedict::cli
