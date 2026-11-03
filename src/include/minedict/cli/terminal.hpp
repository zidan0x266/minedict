// minedict - BSD 3-Clause License; see README.
//
// Everything that touches the operating system's notion of a terminal.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "minedict/cli/ansi.hpp"
#include "minedict/cli/options.hpp"

namespace minedict::cli {

struct TerminalSize {
  std::size_t columns;
  std::size_t rows;
};

[[nodiscard]] bool stdin_is_terminal() noexcept;
[[nodiscard]] bool stdout_is_terminal() noexcept;
[[nodiscard]] std::optional<TerminalSize> terminal_size() noexcept;

[[nodiscard]] std::optional<std::string> environment(std::string_view name);

/// Decide how much colour to emit, honouring NO_COLOR, TERM and COLORTERM.
[[nodiscard]] ColorMode detect_color_mode(ColorChoice choice, bool is_terminal);

/// One-time terminal setup (virtual terminal sequences on Windows,
/// ignoring SIGPIPE elsewhere).
void prepare_terminal() noexcept;

/// Print `text` on stdout; long output goes through the pager when allowed.
void emit(std::string_view text, bool allow_pager);

[[nodiscard]] std::filesystem::path home_directory();
[[nodiscard]] std::filesystem::path config_directory();  // ~/.config/mdict
[[nodiscard]] std::filesystem::path state_directory();   // ~/.local/state/mdict
[[nodiscard]] std::filesystem::path expand_tilde(std::string_view path);

}  // namespace minedict::cli
