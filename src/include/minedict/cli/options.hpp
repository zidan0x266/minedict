// minedict - BSD 3-Clause License; see README.
//
// Command-line parsing. No dependencies, no surprises: `mdict [options]
// [command] [arguments...]`.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "minedict/error.hpp"

namespace minedict::cli {

enum class Command { lookup, search, list, info, extract, repl, help, version };
enum class ColorChoice { automatic, always, never };
enum class SearchMode { prefix, contains };

struct Options {
  Command command{Command::lookup};
  bool command_explicit{false};
  std::vector<std::filesystem::path> dictionaries;
  std::vector<std::string> arguments;  // words, patterns or resource names
  ColorChoice color{ColorChoice::automatic};
  std::string theme{"dracula"};
  bool raw{false};
  std::optional<std::size_t> width;
  bool follow_links{true};
  std::optional<std::size_t> limit;  // per-command default when unset
  SearchMode search_mode{SearchMode::prefix};
  bool pager{true};
  std::optional<std::filesystem::path> output;
  bool show_images{true};
  bool show_sounds{false};  // ♪ markers for sound:// links
  bool use_css{true};  // apply the dictionary's own stylesheet
  bool verbose{false};
};

[[nodiscard]] std::optional<Command> parse_command(std::string_view word) noexcept;
[[nodiscard]] constexpr std::string_view to_string(Command command) noexcept {
  switch (command) {
    case Command::lookup: return "lookup";
    case Command::search: return "search";
    case Command::list: return "list";
    case Command::info: return "info";
    case Command::extract: return "extract";
    case Command::repl: return "repl";
    case Command::help: return "help";
    case Command::version: return "version";
  }
  return "?";
}

/// Parse everything after the program name.
[[nodiscard]] Result<Options> parse_options(std::span<const std::string_view> args);

[[nodiscard]] std::string usage_text(std::string_view program);
[[nodiscard]] std::string_view version_string() noexcept;

}  // namespace minedict::cli
