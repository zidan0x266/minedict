// minedict - error type shared by every layer.
//
// Fallible operations return `Result<T>` (std::expected); nothing throws
// across module boundaries.
#pragma once

#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace minedict {

enum class ErrorKind {
  invalid_argument,  // the caller asked for something impossible
  not_found,         // no such key, file or resource
  io,                // the operating system refused
  format,            // the file is not a well-formed MDX/MDD
  unsupported,       // valid, but a feature we do not implement (yet)
  internal,
};

[[nodiscard]] constexpr std::string_view to_string(ErrorKind kind) noexcept {
  switch (kind) {
    case ErrorKind::invalid_argument: return "invalid argument";
    case ErrorKind::not_found: return "not found";
    case ErrorKind::io: return "i/o error";
    case ErrorKind::format: return "format error";
    case ErrorKind::unsupported: return "unsupported";
    case ErrorKind::internal: return "internal error";
  }
  return "error";
}

struct Error {
  ErrorKind kind{ErrorKind::internal};
  std::string message;

  [[nodiscard]] bool operator==(const Error&) const = default;
};

template <class T>
using Result = std::expected<T, Error>;

/// Build the error branch of a `Result`.
[[nodiscard]] inline std::unexpected<Error> fail(ErrorKind kind, std::string message) {
  return std::unexpected(Error{.kind = kind, .message = std::move(message)});
}

template <class... Args>
[[nodiscard]] std::unexpected<Error> fail(ErrorKind kind, std::format_string<Args...> fmt, Args&&... args) {
  return fail(kind, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace minedict

template <>
struct std::formatter<minedict::Error> : std::formatter<std::string_view> {
  auto format(const minedict::Error& error, std::format_context& ctx) const {
    return std::formatter<std::string_view>::format(error.message, ctx);
  }
};
