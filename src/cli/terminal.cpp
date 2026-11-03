// minedict - BSD 3-Clause License; see README.
#include "minedict/cli/terminal.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#else
#include <csignal>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "minedict/cli/compat.hpp"
#include "minedict/cli/text.hpp"

namespace minedict::cli {
namespace {

#ifdef _WIN32
bool is_terminal(int fd) noexcept { return _isatty(fd) != 0; }
constexpr int kStdinFd = 0;
constexpr int kStdoutFd = 1;
#else
bool is_terminal(int fd) noexcept { return ::isatty(fd) != 0; }
constexpr int kStdinFd = STDIN_FILENO;
constexpr int kStdoutFd = STDOUT_FILENO;
#endif

std::string pager_command() {
  for (const std::string_view name : {"MINEDICT_PAGER", "PAGER"}) {
    if (const auto value = environment(name); value && !text::trim(*value).empty()) return *value;
  }
  return "less";
}

bool pipe_to_pager(std::string_view text) {
  std::string command = pager_command();
  if (command == "cat") return false;

  const std::string program = std::filesystem::path(command.substr(0, command.find(' '))).filename().string();
  if (program == "less" || program == "less.exe") {
    command += " -R";                                           // keep colours
    if (!environment("LESS")) command += " -F -X";              // quit if it fits, no screen clearing
  }

#ifdef _WIN32
  std::FILE* pipe = _popen(command.c_str(), "w");
#else
  std::FILE* pipe = ::popen(command.c_str(), "w");
#endif
  if (pipe == nullptr) return false;
  write(pipe, text);
  std::fflush(pipe);
#ifdef _WIN32
  _pclose(pipe);
#else
  ::pclose(pipe);
#endif
  return true;
}

}  // namespace

bool stdin_is_terminal() noexcept { return is_terminal(kStdinFd); }
bool stdout_is_terminal() noexcept { return is_terminal(kStdoutFd); }

std::optional<TerminalSize> terminal_size() noexcept {
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) == 0) return std::nullopt;
  const int columns = info.srWindow.Right - info.srWindow.Left + 1;
  const int rows = info.srWindow.Bottom - info.srWindow.Top + 1;
  if (columns <= 0 || rows <= 0) return std::nullopt;
  return TerminalSize{static_cast<std::size_t>(columns), static_cast<std::size_t>(rows)};
#else
  winsize size{};
  if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || size.ws_col == 0) return std::nullopt;
  return TerminalSize{size.ws_col, size.ws_row == 0 ? 24U : size.ws_row};
#endif
}

std::optional<std::string> environment(std::string_view name) {
  const std::string key(name);
  const char* value = std::getenv(key.c_str());  // NOLINT(concurrency-mt-unsafe)
  if (value == nullptr) return std::nullopt;
  return std::string(value);
}

ColorMode detect_color_mode(ColorChoice choice, bool is_terminal) {
  if (choice == ColorChoice::never) return ColorMode::none;

  const auto term = environment("TERM").value_or("");
  if (choice == ColorChoice::automatic) {
    if (!is_terminal) return ColorMode::none;
    if (const auto no_color = environment("NO_COLOR"); no_color && !no_color->empty()) return ColorMode::none;
    if (term == "dumb") return ColorMode::none;
  }

  const std::string colorterm = text::to_lower_ascii(environment("COLORTERM").value_or(""));
  if (colorterm == "truecolor" || colorterm == "24bit") return ColorMode::truecolor;
  if (term.contains("truecolor") || term.contains("direct")) return ColorMode::truecolor;

  const std::string program = text::to_lower_ascii(environment("TERM_PROGRAM").value_or(""));
  for (const std::string_view truecolor_program : {"iterm", "wezterm", "vscode", "hyper", "ghostty", "kitty", "alacritty",
                                                  "rio", "tabby", "warp"}) {
    if (program.contains(truecolor_program)) return ColorMode::truecolor;
  }
  if (environment("KITTY_WINDOW_ID") || environment("WT_SESSION") || environment("KONSOLE_VERSION")) {
    return ColorMode::truecolor;
  }
  return ColorMode::ansi256;
}

void prepare_terminal() noexcept {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode) != 0) {
    SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
#else
  std::signal(SIGPIPE, SIG_IGN);  // a pager closed early must not kill us
#endif
}

void emit(std::string_view text, bool allow_pager) {
  if (allow_pager && stdout_is_terminal()) {
    const auto size = terminal_size();
    const std::size_t rows = size ? size->rows : 24;
    const std::size_t lines = static_cast<std::size_t>(std::ranges::count(text, '\n')) + 1;
    if (lines + 1 > rows && pipe_to_pager(text)) return;
  }
  write(stdout, text);
  std::fflush(stdout);
}

std::filesystem::path home_directory() {
  if (const auto home = environment("HOME"); home && !home->empty()) return *home;
  if (const auto profile = environment("USERPROFILE"); profile && !profile->empty()) return *profile;
  return std::filesystem::current_path();
}

std::filesystem::path config_directory() {
#ifdef _WIN32
  if (const auto appdata = environment("APPDATA"); appdata && !appdata->empty()) {
    return std::filesystem::path(*appdata) / "minedict";
  }
#endif
  if (const auto xdg = environment("XDG_CONFIG_HOME"); xdg && !xdg->empty()) {
    return std::filesystem::path(*xdg) / "minedict";
  }
  return home_directory() / ".config" / "minedict";
}

std::filesystem::path state_directory() {
#ifdef _WIN32
  if (const auto local = environment("LOCALAPPDATA"); local && !local->empty()) {
    return std::filesystem::path(*local) / "minedict";
  }
#endif
  if (const auto xdg = environment("XDG_STATE_HOME"); xdg && !xdg->empty()) {
    return std::filesystem::path(*xdg) / "minedict";
  }
  return home_directory() / ".local" / "state" / "minedict";
}

std::filesystem::path expand_tilde(std::string_view path) {
  if (path == "~") return home_directory();
  if (path.starts_with("~/") || path.starts_with("~\\")) return home_directory() / std::string(path.substr(2));
  return std::filesystem::path(std::string(path));
}

}  // namespace minedict::cli
