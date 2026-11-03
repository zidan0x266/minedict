// minedict - byte views and a bounds-checked big-endian reader.
//
// MDict files are big-endian throughout, except for the header checksum and
// the compression tag, which are little-endian.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "minedict/error.hpp"

namespace minedict {

using Bytes = std::vector<std::byte>;
using ByteView = std::span<const std::byte>;

[[nodiscard]] inline ByteView as_bytes(std::string_view text) noexcept {
  return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

[[nodiscard]] inline std::string_view as_chars(ByteView bytes) noexcept {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// Interpret up to eight bytes as a big-endian unsigned integer.
[[nodiscard]] constexpr std::uint64_t read_be(ByteView bytes) noexcept {
  std::uint64_t value = 0;
  for (const std::byte b : bytes.first(bytes.size() < 8 ? bytes.size() : 8)) {
    value = (value << 8U) | static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(b));
  }
  return value;
}

/// Interpret up to eight bytes as a little-endian unsigned integer.
[[nodiscard]] constexpr std::uint64_t read_le(ByteView bytes) noexcept {
  std::uint64_t value = 0;
  const std::size_t n = bytes.size() < 8 ? bytes.size() : 8;
  for (std::size_t i = n; i-- > 0;) {
    value = (value << 8U) | static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[i]));
  }
  return value;
}

/// Width of the integers in a file: 4 bytes before engine 2.0, 8 bytes after.
enum class NumberWidth : std::uint8_t { four = 4, eight = 8 };

/// A cursor over a byte view. Reads past the end set a sticky error and
/// return zero, so a parser can issue a run of reads and check `status()`
/// once at the end instead of after every field.
class Reader {
 public:
  constexpr explicit Reader(ByteView data) noexcept : data_(data) {}

  [[nodiscard]] std::uint8_t u8() { return static_cast<std::uint8_t>(integer(1)); }
  [[nodiscard]] std::uint16_t u16() { return static_cast<std::uint16_t>(integer(2)); }
  [[nodiscard]] std::uint32_t u32() { return static_cast<std::uint32_t>(integer(4)); }
  [[nodiscard]] std::uint64_t u64() { return integer(8); }
  [[nodiscard]] std::uint32_t u32_le() {
    const ByteView bytes = take(4);
    return bytes.empty() ? 0U : static_cast<std::uint32_t>(read_le(bytes));
  }
  [[nodiscard]] std::uint64_t number(NumberWidth width) { return integer(static_cast<std::size_t>(width)); }

  /// The next `count` bytes, or an empty view (and an error) when they are
  /// not there.
  [[nodiscard]] ByteView take(std::size_t count) {
    if (!fits(count)) return {};
    const ByteView out = data_.subspan(position_, count);
    position_ += count;
    return out;
  }

  void skip(std::size_t count) {
    if (fits(count)) position_ += count;
  }

  [[nodiscard]] constexpr std::size_t position() const noexcept { return position_; }
  [[nodiscard]] constexpr std::size_t remaining() const noexcept { return data_.size() - position_; }
  [[nodiscard]] constexpr bool done() const noexcept { return position_ == data_.size(); }
  [[nodiscard]] constexpr bool ok() const noexcept { return !error_.has_value(); }

  /// Success, or the first overrun that happened.
  [[nodiscard]] Result<void> status() const {
    if (error_) return std::unexpected(*error_);
    return {};
  }

 private:
  [[nodiscard]] bool fits(std::size_t count) {
    if (error_) return false;
    if (count > remaining()) {
      error_ = Error{.kind = ErrorKind::format,
                     .message = std::format("truncated data: wanted {} bytes at offset {} but only {} remain", count,
                                            position_, remaining())};
      return false;
    }
    return true;
  }

  [[nodiscard]] std::uint64_t integer(std::size_t width) {
    const ByteView bytes = take(width);
    return bytes.empty() ? 0U : read_be(bytes);
  }

  ByteView data_;
  std::size_t position_{0};
  std::optional<Error> error_;
};

}  // namespace minedict
