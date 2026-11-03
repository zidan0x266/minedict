// minedict - BSD 3-Clause License; see README.
//
// Printing helpers: std::print where the standard library provides it,
// std::format plus fwrite otherwise.
#pragma once

#include <version>

#include <cstdio>
#include <format>
#include <string_view>
#include <utility>

#if defined(__cpp_lib_print) && __has_include(<print>)
#include <print>
#define MINEDICT_HAS_STD_PRINT 1
#else
#define MINEDICT_HAS_STD_PRINT 0
#endif

namespace minedict::cli {

/// Write raw bytes to a stream (embedded NULs are preserved).
inline void write(std::FILE* stream, std::string_view text) {
  if (!text.empty()) {
    std::fwrite(text.data(), 1, text.size(), stream);
  }
}

template <class... Args>
void print(std::FILE* stream, std::format_string<Args...> fmt, Args&&... args) {
#if MINEDICT_HAS_STD_PRINT
  std::print(stream, fmt, std::forward<Args>(args)...);
#else
  write(stream, std::format(fmt, std::forward<Args>(args)...));
#endif
}

template <class... Args>
void print(std::format_string<Args...> fmt, Args&&... args) {
  cli::print(stdout, fmt, std::forward<Args>(args)...);
}

template <class... Args>
void println(std::FILE* stream, std::format_string<Args...> fmt, Args&&... args) {
#if MINEDICT_HAS_STD_PRINT
  std::println(stream, fmt, std::forward<Args>(args)...);
#else
  write(stream, std::format(fmt, std::forward<Args>(args)...));
  std::fputc('\n', stream);
#endif
}

template <class... Args>
void println(std::format_string<Args...> fmt, Args&&... args) {
  cli::println(stdout, fmt, std::forward<Args>(args)...);
}

inline void println(std::FILE* stream = stdout) { std::fputc('\n', stream); }

/// Helper for std::visit with a set of lambdas.
template <class... Fs>
struct overloaded : Fs... {
  using Fs::operator()...;
};
template <class... Fs>
overloaded(Fs...) -> overloaded<Fs...>;

}  // namespace minedict::cli
