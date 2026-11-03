// minedict - code point utilities: UTF-8 and UTF-16LE codecs, character
// classes and the simple case folding used for headword comparison.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "minedict/bytes.hpp"

namespace minedict::unicode {

inline constexpr char32_t replacement_character = 0xFFFD;

struct Utf8Char {
  char32_t code_point;
  std::uint8_t size;  // bytes consumed, always at least one
};

/// Decode the code point starting at `pos`. Malformed input yields U+FFFD
/// and consumes one byte, so a loop always makes progress.
[[nodiscard]] Utf8Char decode_utf8(std::string_view text, std::size_t pos) noexcept;

void append_utf8(std::string& out, char32_t code_point);
[[nodiscard]] std::string encode_utf8(char32_t code_point);
[[nodiscard]] std::u32string to_u32(std::string_view text);
[[nodiscard]] std::string from_u32(std::u32string_view text);

/// Decode UTF-16LE bytes to UTF-8. Lone surrogates and a trailing odd byte
/// become U+FFFD rather than errors; dictionaries are not always tidy.
[[nodiscard]] std::string utf16le_to_utf8(ByteView bytes);

/// Whitespace, including the Unicode space separators dictionaries like to use.
[[nodiscard]] constexpr bool is_space(char32_t cp) noexcept {
  switch (cp) {
    case 0x0009: case 0x000A: case 0x000B: case 0x000C: case 0x000D: case 0x0020:
    case 0x00A0:  // no-break space
    case 0x1680:  // ogham space mark
    case 0x2028: case 0x2029:  // line / paragraph separator
    case 0x202F:  // narrow no-break space
    case 0x205F:  // medium mathematical space
    case 0x3000:  // ideographic space
    case 0x200B:  // zero width space
    case 0xFEFF:  // byte order mark used as zero width no-break space
      return true;
    default:
      return cp >= 0x2000 && cp <= 0x200A;  // en quad .. hair space
  }
}

/// ASCII punctuation plus the common typographic and CJK marks.
[[nodiscard]] constexpr bool is_punctuation(char32_t cp) noexcept {
  if (cp < 0x80) {
    return (cp >= 0x21 && cp <= 0x2F) || (cp >= 0x3A && cp <= 0x40) || (cp >= 0x5B && cp <= 0x60) ||
           (cp >= 0x7B && cp <= 0x7E);
  }
  switch (cp) {
    case 0x00B7: case 0x00AB: case 0x00BB: case 0x00A1: case 0x00BF:  // · « » ¡ ¿
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014:  // hyphens and dashes
    case 0x2018: case 0x2019: case 0x201A: case 0x201C: case 0x201D:  // quotation marks
    case 0x201E: case 0x2022: case 0x2026: case 0x2039: case 0x203A:  // „ • … ‹ ›
    case 0x3001: case 0x3002: case 0x300C: case 0x300D:               // 、 。 「 」
    case 0xFF01: case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B: case 0xFF1F:  // fullwidth ! , . : ; ?
      return true;
    default:
      return false;
  }
}

/// Lower-case a letter from the Latin, Greek or Cyrillic blocks; anything
/// else is returned unchanged.
[[nodiscard]] constexpr char32_t fold_case(char32_t cp) noexcept {
  if (cp >= 0x41 && cp <= 0x5A) return cp + 32;  // A-Z
  if (cp < 0x80) return cp;
  if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32;        // Latin-1 capitals
  if (cp >= 0x100 && cp <= 0x137) return cp | 1U;                    // Latin Extended-A, even = capital
  if (cp >= 0x139 && cp <= 0x148) return (cp % 2 == 1) ? cp + 1 : cp;
  if (cp >= 0x14A && cp <= 0x177) return cp | 1U;
  if (cp == 0x178) return 0xFF;                                      // Ÿ -> ÿ
  if (cp >= 0x179 && cp <= 0x17E) return (cp % 2 == 1) ? cp + 1 : cp;
  if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) return cp + 32;     // Greek
  if (cp >= 0x410 && cp <= 0x42F) return cp + 32;                    // Cyrillic
  if (cp >= 0x400 && cp <= 0x40F) return cp + 80;
  if (cp >= 0xFF21 && cp <= 0xFF3A) return cp + 32;                  // fullwidth A-Z
  return cp;
}

}  // namespace minedict::unicode
