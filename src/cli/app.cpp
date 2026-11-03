// minedict - BSD 3-Clause License; see README.
#include "minedict/cli/app.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

#include "minedict/cli/compat.hpp"
#include "minedict/cli/repl.hpp"
#include "minedict/cli/terminal.hpp"
#include "minedict/cli/text.hpp"

namespace minedict::cli {
namespace {

constexpr std::size_t kNoWrap = 1'000'000;
constexpr std::size_t kMaxComfortableWidth = 100;
constexpr std::size_t kSuggestionCount = 8;

std::size_t choose_width(const Options& options, bool is_terminal) {
  if (options.width) return *options.width == 0 ? kNoWrap : *options.width;
  if (is_terminal) {
    if (const auto size = terminal_size()) return std::min(size->columns, kMaxComfortableWidth);
  }
  return 80;
}

void complain(std::string_view message) { cli::println(stderr, "minedict: {}", message); }

}  // namespace

App::App(Options options) : options_(std::move(options)), theme_(Theme::dracula()) {}

int App::run() {
  prepare_terminal();

  if (options_.command == Command::help) {
    cli::print("{}", usage_text("minedict"));
    return exit_ok;
  }
  if (options_.command == Command::version) {
    cli::println("minedict {}", version_string());
    return exit_ok;
  }

  auto theme = Theme::named(options_.theme);
  if (!theme) {
    complain(std::format("unknown theme '{}' (available: dracula, plain)", options_.theme));
    return exit_usage;
  }
  theme_ = std::move(*theme);

  const bool terminal = stdout_is_terminal();
  color_ = detect_color_mode(options_.color, terminal);
  width_ = choose_width(options_, terminal);

  if (const int code = load_dictionaries(); code != exit_ok) return code;

  switch (options_.command) {
    case Command::lookup: return lookup();
    case Command::search: return search();
    case Command::list: return list();
    case Command::info: return info();
    case Command::extract: return extract();
    case Command::repl: return repl();
    case Command::help:
    case Command::version: break;
  }
  return exit_ok;
}

int App::load_dictionaries() {
  std::vector<std::filesystem::path> sources = options_.dictionaries;
  if (sources.empty()) sources = Shelf::default_sources();
  if (sources.empty()) {
    complain("no dictionary given");
    cli::println(stderr, "  pass -d <file.mdx>, set MINEDICT_DICTS, or list paths in {}",
            (config_directory() / "dictionaries").string());
    return exit_no_dictionary;
  }

  for (const auto& source : sources) {
    const auto started = std::chrono::steady_clock::now();
    if (const auto status = shelf_.add(source); !status) {
      complain(status.error().message);
      continue;
    }
    if (options_.verbose) {
      const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - started;
      cli::println(stderr, "minedict: loaded {} in {:.0f} ms", source.string(), elapsed.count());
    }
  }
  if (shelf_.empty()) {
    complain("no dictionary could be opened");
    return exit_no_dictionary;
  }
  return exit_ok;
}

bool App::append_lookup(std::string_view word, const Presenter& presenter, const HtmlRenderer& renderer,
                        std::string& out) const {
  const auto outcome = shelf_.lookup(word, LookupOptions{.follow_links = options_.follow_links});
  for (const Error& problem : outcome.problems) complain(problem.message);

  bool found = outcome.found();
  if (found) {
    if (options_.raw) {
      for (const Shelf::Hit& hit : outcome.hits) {
        for (const Entry& entry : hit.entries) {
          out += entry.definition;
          out += '\n';
        }
      }
    } else {
      out += presenter.entries(outcome, renderer);
    }
  }

  // MDD files answer with resources rather than definitions; only ask them
  // when the query looks like a resource path.
  const bool looks_like_resource = word.find_first_of("\\/.") != std::string_view::npos;
  for (const Dictionary& dictionary : shelf_.dictionaries()) {
    if (dictionary.metadata().kind != Kind::mdd || !looks_like_resource) continue;
    if (const auto bytes = dictionary.resource(word)) {
      found = true;
      if (!options_.raw) out += presenter.resource_summary(dictionary, word, bytes->size());
    }
  }

  if (!found) {
    const auto suggestions = shelf_.suggest(word, kSuggestionCount);
    std::vector<Shelf::Match> prefixes;
    if (suggestions.empty()) prefixes = shelf_.find_prefix(word, kSuggestionCount);
    out += presenter.not_found(word, suggestions, prefixes);
  }
  return found;
}

int App::lookup() {
  Presenter presenter{theme_, color_, width_};
  presenter.set_css(options_.use_css);
  const HtmlRenderer renderer = presenter.renderer(2, options_.show_images, options_.show_sounds);
  std::string out;
  bool all_found = true;
  bool first = true;
  for (const std::string& word : options_.arguments) {
    if (!std::exchange(first, false)) out += '\n';
    all_found = append_lookup(word, presenter, renderer, out) && all_found;
  }
  emit(out, options_.pager);
  return all_found ? exit_ok : exit_not_found;
}

int App::batch() {
  // Words arrive on stdin, one per line; results stream out as they come.
  Presenter presenter{theme_, color_, width_};
  presenter.set_css(options_.use_css);
  const HtmlRenderer renderer = presenter.renderer(2, options_.show_images, options_.show_sounds);
  bool all_found = true;
  bool first = true;
  std::string line;
  while (std::getline(std::cin, line)) {
    const std::string_view word = text::trim(line);
    if (word.empty()) continue;
    std::string out;
    if (!std::exchange(first, false)) out += '\n';
    all_found = append_lookup(word, presenter, renderer, out) && all_found;
    write(stdout, out);
    std::fflush(stdout);
  }
  return all_found ? exit_ok : exit_not_found;
}

int App::search() {
  Presenter presenter{theme_, color_, width_};
  presenter.set_css(options_.use_css);
  const std::size_t limit = options_.limit.value_or(50);
  const bool contains = options_.search_mode == SearchMode::contains;
  const bool terminal = stdout_is_terminal();
  std::string out;
  bool any = false;
  for (const std::string& pattern : options_.arguments) {
    const auto matches = contains ? shelf_.find_containing(pattern, limit) : shelf_.find_prefix(pattern, limit);
    if (matches.empty()) {
      out += presenter.problem(std::format("no headwords {} “{}”", contains ? "containing" : "starting with", pattern));
      out += '\n';
      continue;
    }
    any = true;
    if (terminal) {
      out += presenter.matches(pattern, matches, false, shelf_.size() > 1);
    } else {
      for (const Shelf::Match& match : matches) {
        out += match.key.key;
        out += '\n';
      }
    }
  }
  emit(out, options_.pager);
  return any ? exit_ok : exit_not_found;
}

int App::list() {
  const std::size_t limit = options_.limit.value_or(0);
  std::string out;
  std::size_t count = 0;
  for (const Dictionary& dictionary : shelf_.dictionaries()) {
    if (options_.verbose) {
      cli::println(stderr, "# {} ({} keys)", dictionary.metadata().title, dictionary.size());
    }
    for (std::uint32_t position = 0; position < dictionary.size(); ++position) {
      if (limit != 0 && count >= limit) break;
      out += dictionary.key_at(position).key;
      out += '\n';
      ++count;
    }
  }
  emit(out, options_.pager);
  return exit_ok;
}

int App::info() {
  Presenter presenter{theme_, color_, width_};
  presenter.set_css(options_.use_css);
  std::string out;
  bool first = true;
  for (const Dictionary& dictionary : shelf_.dictionaries()) {
    if (!std::exchange(first, false)) out += '\n';
    out += presenter.info(dictionary);
  }
  emit(out, options_.pager);
  return exit_ok;
}

int App::extract() {
  const bool to_stdout = options_.output && options_.output->string() == "-";
  if (!options_.output && stdout_is_terminal()) {
    complain("refusing to write binary data to the terminal; add -o <file> (or -o - to force stdout)");
    return exit_usage;
  }
  if (options_.output && !to_stdout && options_.arguments.size() > 1) {
    complain("-o names a single file but several resources were requested");
    return exit_usage;
  }

  bool any_mdd = std::ranges::any_of(shelf_.dictionaries(),
                                     [](const Dictionary& d) { return d.metadata().kind == Kind::mdd; });
  if (!any_mdd) {
    complain("extract needs an .mdd resource file (-d resources.mdd)");
    return exit_no_dictionary;
  }

  bool all_found = true;
  for (const std::string& name : options_.arguments) {
    bool found = false;
    for (const Dictionary& dictionary : shelf_.dictionaries()) {
      if (dictionary.metadata().kind != Kind::mdd) continue;
      auto bytes = dictionary.resource(name);
      if (!bytes) {
        if (bytes.error().kind != ErrorKind::not_found) complain(bytes.error().message);
        continue;
      }
      found = true;
      const std::string_view data{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
      if (!options_.output || to_stdout) {
        write(stdout, data);
        std::fflush(stdout);
      } else {
        std::ofstream file(*options_.output, std::ios::binary);
        if (!file) {
          complain(std::format("cannot write {}", options_.output->string()));
          return exit_usage;
        }
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        cli::println(stderr, "minedict: wrote {} ({}) to {}", name, text::human_size(bytes->size()), options_.output->string());
      }
      break;
    }
    if (!found) {
      complain(std::format("no resource named '{}'", name));
      all_found = false;
    }
  }
  return all_found ? exit_ok : exit_not_found;
}

int App::repl() {
  if (!stdin_is_terminal()) return batch();
  Repl repl{shelf_, theme_, color_,
            Repl::Settings{.raw = options_.raw,
                           .follow_links = options_.follow_links,
                           .limit = options_.limit.value_or(30),
                           .pager = options_.pager,
                           .show_images = options_.show_images,
                           .show_sounds = options_.show_sounds,
                           .use_css = options_.use_css,
                           .fixed_width = options_.width}};
  return repl.run();
}

}  // namespace minedict::cli
