// minedict - text encodings of dictionary payloads.
//
// Keys and definitions are stored in the charset the header declares:
// UTF-8 or UTF-16LE for most files, GBK/GB18030 or Big5 for older Chinese
// ones. The two Unicode forms are decoded natively; legacy code pages go
// through iconv when the build has it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "minedict/bytes.hpp"

namespace minedict {

enum class Charset : std::uint8_t { utf8, utf16le, gb18030, big5, latin1, other };

struct Encoding {
  Charset charset{Charset::utf8};
  std::string name{"UTF-8"};  // canonical display name (and iconv name for `other`)

  /// Interpret the header's `Encoding` attribute. Unknown names are kept
  /// verbatim as `Charset::other` for iconv to try.
  [[nodiscard]] static Encoding from_name(std::string_view declared);
  [[nodiscard]] static Encoding utf8() { return {}; }
  [[nodiscard]] static Encoding utf16le() { return {Charset::utf16le, "UTF-16LE"}; }

  /// Bytes per code unit: 2 for UTF-16, 1 otherwise. Also the width of the
  /// NUL terminator after each key and record.
  [[nodiscard]] constexpr std::size_t unit_size() const noexcept { return charset == Charset::utf16le ? 2 : 1; }
  [[nodiscard]] constexpr bool is_unicode() const noexcept {
    return charset == Charset::utf8 || charset == Charset::utf16le;
  }

  [[nodiscard]] bool operator==(const Encoding&) const = default;
};

/// Whether legacy code pages can be decoded in this build (iconv present).
[[nodiscard]] bool legacy_charsets_supported() noexcept;

/// Converts bytes in one encoding to UTF-8, holding any converter state.
/// Not thread-safe; create one per thread or guard it.
class Transcoder {
 public:
  [[nodiscard]] static Result<Transcoder> open(const Encoding& encoding);

  Transcoder(Transcoder&& other) noexcept;
  Transcoder& operator=(Transcoder&& other) noexcept;
  Transcoder(const Transcoder&) = delete;
  Transcoder& operator=(const Transcoder&) = delete;
  ~Transcoder();

  [[nodiscard]] const Encoding& encoding() const noexcept { return encoding_; }

  /// Undecodable bytes become U+FFFD; the conversion itself never fails
  /// once the transcoder is open.
  [[nodiscard]] std::string to_utf8(ByteView bytes);

 private:
  explicit Transcoder(Encoding encoding, void* handle) noexcept;

  Encoding encoding_;
  void* handle_{nullptr};  // iconv_t when a legacy charset is in use
};

}  // namespace minedict
