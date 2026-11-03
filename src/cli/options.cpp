// minedict - BSD 3-Clause License; see README.
#include "minedict/cli/options.hpp"

#include <charconv>
#include <format>

#include "minedict/cli/text.hpp"

#include "minedict/version.hpp"

namespace minedict::cli {
namespace {

class Parser {
 public:
  explicit Parser(std::span<const std::string_view> args) : args_(args) {}

  Result<Options> run() {
    while (index_ < args_.size()) {
      const std::string_view arg = args_[index_++];
      Result<void> status;
      if (literal_ || arg == "-" || !arg.starts_with('-')) {
        status = positional(arg);
      } else if (arg == "--") {
        literal_ = true;
      } else if (arg.starts_with("--")) {
        status = long_option(arg.substr(2));
      } else {
        status = short_options(arg.substr(1));
      }
      if (!status) return std::unexpected(status.error());
    }
    if (const auto status = validate(); !status) return std::unexpected(status.error());
    return std::move(options_);
  }

 private:
  Result<void> positional(std::string_view arg) {
    const std::string lowered = text::to_lower_ascii(arg);
    if (!literal_ && (lowered.ends_with(".mdx") || lowered.ends_with(".mdd"))) {
      options_.dictionaries.emplace_back(std::string(arg));
      return {};
    }
    if (!literal_ && !options_.command_explicit && options_.arguments.empty()) {
      if (const auto command = parse_command(arg)) {
        options_.command = *command;
        options_.command_explicit = true;
        return {};
      }
    }
    options_.arguments.emplace_back(arg);
    return {};
  }

  Result<std::string_view> value_for(std::string_view name, std::optional<std::string_view> inline_value) {
    if (inline_value) return *inline_value;
    if (index_ >= args_.size()) {
      return fail(ErrorKind::invalid_argument, std::format("option '{}' needs a value", name));
    }
    return args_[index_++];
  }

  Result<std::size_t> number_for(std::string_view name, std::optional<std::string_view> inline_value) {
    const auto raw = value_for(name, inline_value);
    if (!raw) return std::unexpected(raw.error());
    std::size_t value = 0;
    const auto [ptr, ec] = std::from_chars(raw->data(), raw->data() + raw->size(), value);
    if (ec != std::errc{} || ptr != raw->data() + raw->size()) {
      return fail(ErrorKind::invalid_argument, std::format("option '{}' expects a number, got '{}'", name, *raw));
    }
    return value;
  }

  Result<void> set_color(std::string_view value) {
    const std::string lowered = text::to_lower_ascii(value);
    if (lowered == "auto" || lowered == "automatic") {
      options_.color = ColorChoice::automatic;
    } else if (lowered == "always" || lowered == "yes" || lowered == "on" || lowered == "force") {
      options_.color = ColorChoice::always;
    } else if (lowered == "never" || lowered == "no" || lowered == "off" || lowered == "none") {
      options_.color = ColorChoice::never;
    } else {
      return fail(ErrorKind::invalid_argument, std::format("--color expects auto, always or never, got '{}'", value));
    }
    return {};
  }

  Result<void> long_option(std::string_view body) {
    std::optional<std::string_view> inline_value;
    std::string_view name = body;
    if (const std::size_t eq = body.find('='); eq != std::string_view::npos) {
      name = body.substr(0, eq);
      inline_value = body.substr(eq + 1);
    }
    const std::string display = std::format("--{}", name);

    const auto with_value = [&](auto&& apply) -> Result<void> {
      const auto value = value_for(display, inline_value);
      if (!value) return std::unexpected(value.error());
      return apply(*value);
    };
    const auto with_number = [&](auto&& apply) -> Result<void> {
      const auto value = number_for(display, inline_value);
      if (!value) return std::unexpected(value.error());
      apply(*value);
      return {};
    };

    if (name == "dict" || name == "dictionary") {
      return with_value([&](std::string_view v) -> Result<void> {
        options_.dictionaries.emplace_back(std::string(v));
        return {};
      });
    }
    if (name == "color" || name == "colour") {
      return with_value([&](std::string_view v) { return set_color(v); });
    }
    if (name == "no-color" || name == "no-colour") {
      options_.color = ColorChoice::never;
      return {};
    }
    if (name == "theme") {
      return with_value([&](std::string_view v) -> Result<void> {
        options_.theme = std::string(v);
        return {};
      });
    }
    if (name == "width") {
      return with_number([&](std::size_t v) { options_.width = v; });
    }
    if (name == "limit") {
      return with_number([&](std::size_t v) { options_.limit = v; });
    }
    if (name == "output") {
      return with_value([&](std::string_view v) -> Result<void> {
        options_.output = std::filesystem::path(std::string(v));
        return {};
      });
    }
    if (inline_value) {
      return fail(ErrorKind::invalid_argument, std::format("option '{}' does not take a value", display));
    }
    if (name == "raw" || name == "html") {
      options_.raw = true;
    } else if (name == "no-follow" || name == "no-links") {
      options_.follow_links = false;
    } else if (name == "contains") {
      options_.search_mode = SearchMode::contains;
    } else if (name == "prefix") {
      options_.search_mode = SearchMode::prefix;
    } else if (name == "pager") {
      options_.pager = true;
    } else if (name == "no-pager") {
      options_.pager = false;
    } else if (name == "no-images") {
      options_.show_images = false;
    } else if (name == "no-css") {
      options_.use_css = false;
    } else if (name == "sounds") {
      options_.show_sounds = true;
    } else if (name == "verbose") {
      options_.verbose = true;
    } else if (name == "interactive") {
      options_.command = Command::repl;
      options_.command_explicit = true;
    } else if (name == "help") {
      options_.command = Command::help;
      options_.command_explicit = true;
    } else if (name == "version") {
      options_.command = Command::version;
      options_.command_explicit = true;
    } else {
      return fail(ErrorKind::invalid_argument, std::format("unknown option '{}'", display));
    }
    return {};
  }

  Result<void> short_options(std::string_view flags) {
    for (std::size_t i = 0; i < flags.size(); ++i) {
      const char flag = flags[i];
      const std::string display = std::format("-{}", flag);
      // Flags that take a value consume the rest of the cluster or the next argument.
      const auto rest = [&]() -> std::optional<std::string_view> {
        if (i + 1 < flags.size()) {
          const auto value = flags.substr(i + 1);
          i = flags.size();
          return value;
        }
        return std::nullopt;
      };
      switch (flag) {
        case 'd': {
          const auto value = value_for(display, rest());
          if (!value) return std::unexpected(value.error());
          options_.dictionaries.emplace_back(std::string(*value));
          break;
        }
        case 'n': {
          const auto value = number_for(display, rest());
          if (!value) return std::unexpected(value.error());
          options_.limit = *value;
          break;
        }
        case 'w': {
          const auto value = number_for(display, rest());
          if (!value) return std::unexpected(value.error());
          options_.width = *value;
          break;
        }
        case 'o': {
          const auto value = value_for(display, rest());
          if (!value) return std::unexpected(value.error());
          options_.output = std::filesystem::path(std::string(*value));
          break;
        }
        case 't': {
          const auto value = value_for(display, rest());
          if (!value) return std::unexpected(value.error());
          options_.theme = std::string(*value);
          break;
        }
        case 'r': options_.raw = true; break;
        case 'c': options_.search_mode = SearchMode::contains; break;
        case 'v': options_.verbose = true; break;
        case 'i':
          options_.command = Command::repl;
          options_.command_explicit = true;
          break;
        case 'h':
          options_.command = Command::help;
          options_.command_explicit = true;
          break;
        case 'V':
          options_.command = Command::version;
          options_.command_explicit = true;
          break;
        default:
          return fail(ErrorKind::invalid_argument, std::format("unknown option '{}'", display));
      }
    }
    return {};
  }

  Result<void> validate() {
    switch (options_.command) {
      case Command::lookup:
      case Command::search:
      case Command::extract:
        if (options_.arguments.empty()) {
          if (options_.command_explicit) {
            return fail(ErrorKind::invalid_argument,
                        std::format("'{}' needs at least one argument", to_string(options_.command)));
          }
          // No command and no words: fall back to the interactive mode (or
          // help); the application decides based on the terminal.
          options_.command = Command::repl;
        }
        break;
      case Command::list:
      case Command::info:
      case Command::repl:
      case Command::help:
      case Command::version:
        if (!options_.arguments.empty()) {
          return fail(ErrorKind::invalid_argument,
                      std::format("'{}' does not take arguments (got '{}')", to_string(options_.command),
                                  options_.arguments.front()));
        }
        break;
    }
    return {};
  }

  std::span<const std::string_view> args_;
  std::size_t index_{0};
  bool literal_{false};
  Options options_;
};

}  // namespace

std::optional<Command> parse_command(std::string_view word) noexcept {
  const std::string lowered = text::to_lower_ascii(word);
  if (lowered == "lookup" || lowered == "look" || lowered == "define" || lowered == "def") return Command::lookup;
  if (lowered == "search" || lowered == "find") return Command::search;
  if (lowered == "list" || lowered == "keys") return Command::list;
  if (lowered == "info" || lowered == "about") return Command::info;
  if (lowered == "extract" || lowered == "resource") return Command::extract;
  if (lowered == "repl" || lowered == "interactive" || lowered == "shell") return Command::repl;
  if (lowered == "help") return Command::help;
  if (lowered == "version") return Command::version;
  return std::nullopt;
}

Result<Options> parse_options(std::span<const std::string_view> args) { return Parser{args}.run(); }

std::string_view version_string() noexcept { return minedict::version; }

std::string usage_text(std::string_view program) {
  return std::format(R"(minedict {0} - a terminal dictionary for MDX/MDD files

Usage:
  {1} [options] <word>...                 look up words
  {1} [options] lookup <word>...          same, when the word is also a command name
  {1} [options] search [-c] <pattern>     list headwords by prefix (or -c: containing)
  {1} [options] list                      print every headword
  {1} [options] info                      describe the loaded dictionaries
  {1} [options] extract <name> [-o FILE]  write an MDD resource to a file or stdout
  {1} [options] repl                      interactive mode (default with a terminal)

Dictionaries:
  -d, --dict <path>      an .mdx/.mdd file or a directory of them (repeatable);
                         bare .mdx/.mdd arguments are accepted too
                         defaults: $MINEDICT_DICTS (colon-separated) and
                         ~/.config/minedict/dictionaries (one path per line)

Output:
  -r, --raw              print the stored HTML instead of rendering it
  -w, --width <n>        wrap at n columns (0 = never wrap; default: terminal width, max 100)
  -n, --limit <n>        maximum results for search (default 50, 0 = all)
  -c, --contains         search for headwords containing the pattern
      --color <when>     auto, always or never
  -t, --theme <name>     dracula (default) or plain
      --no-pager         never pipe long output through $PAGER
      --no-follow        do not follow @@@LINK redirects
      --no-images        hide image placeholders
      --sounds           mark sound:// links with ♪
      --no-css           ignore the dictionary's own stylesheet (<name>.css)
  -o, --output <file>    destination for extract ("-" for stdout)
  -v, --verbose          report load times and dictionary details
  -h, --help             show this help
  -V, --version          show the version

Examples:
  {1} -d oxford.mdx serendipity
  {1} -d ~/dicts search -n 20 seren
  {1} -d sounds.mdd extract '\serendipity.mp3' -o serendipity.mp3
  MINEDICT_DICTS=~/dicts/a.mdx:~/dicts/b.mdx {1}
)",
                     version_string(), program);
}

}  // namespace minedict::cli
