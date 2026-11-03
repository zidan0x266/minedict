#include "minedict/unicode.hpp"

namespace minedict::unicode {

Utf8Char decode_utf8(std::string_view text, std::size_t pos) noexcept {
  constexpr Utf8Char invalid{replacement_character, 1};
  if (pos >= text.size()) return invalid;
  const auto lead = static_cast<unsigned char>(text[pos]);
  if (lead < 0x80) return {lead, 1};

  const auto continuation = [&](std::size_t k) -> int {
    if (pos + k >= text.size()) return -1;
    const auto c = static_cast<unsigned char>(text[pos + k]);
    return (c & 0xC0) == 0x80 ? (c & 0x3F) : -1;
  };

  if ((lead & 0xE0) == 0xC0) {
    if (const int c1 = continuation(1); c1 >= 0) {
      const char32_t cp = (static_cast<char32_t>(lead & 0x1F) << 6) | static_cast<char32_t>(c1);
      if (cp >= 0x80) return {cp, 2};
    }
  } else if ((lead & 0xF0) == 0xE0) {
    const int c1 = continuation(1);
    const int c2 = continuation(2);
    if (c1 >= 0 && c2 >= 0) {
      const char32_t cp = (static_cast<char32_t>(lead & 0x0F) << 12) | (static_cast<char32_t>(c1) << 6) |
                          static_cast<char32_t>(c2);
      if (cp >= 0x800 && (cp < 0xD800 || cp > 0xDFFF)) return {cp, 3};
    }
  } else if ((lead & 0xF8) == 0xF0) {
    const int c1 = continuation(1);
    const int c2 = continuation(2);
    const int c3 = continuation(3);
    if (c1 >= 0 && c2 >= 0 && c3 >= 0) {
      const char32_t cp = (static_cast<char32_t>(lead & 0x07) << 18) | (static_cast<char32_t>(c1) << 12) |
                          (static_cast<char32_t>(c2) << 6) | static_cast<char32_t>(c3);
      if (cp >= 0x10000 && cp <= 0x10FFFF) return {cp, 4};
    }
  }
  return invalid;
}

void append_utf8(std::string& out, char32_t cp) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = replacement_character;
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

std::string encode_utf8(char32_t cp) {
  std::string out;
  append_utf8(out, cp);
  return out;
}

std::u32string to_u32(std::string_view text) {
  std::u32string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size();) {
    const auto [cp, n] = decode_utf8(text, i);
    out.push_back(cp);
    i += n;
  }
  return out;
}

std::string from_u32(std::u32string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char32_t cp : text) append_utf8(out, cp);
  return out;
}

std::string utf16le_to_utf8(ByteView bytes) {
  std::string out;
  out.reserve(bytes.size());
  const auto unit_at = [&](std::size_t i) -> char32_t {
    return static_cast<char32_t>(std::to_integer<std::uint8_t>(bytes[i])) |
           (static_cast<char32_t>(std::to_integer<std::uint8_t>(bytes[i + 1])) << 8);
  };
  std::size_t i = 0;
  for (; i + 1 < bytes.size(); i += 2) {
    const char32_t unit = unit_at(i);
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      if (i + 3 < bytes.size()) {
        if (const char32_t low = unit_at(i + 2); low >= 0xDC00 && low <= 0xDFFF) {
          append_utf8(out, 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
          i += 2;
          continue;
        }
      }
      append_utf8(out, replacement_character);
    } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
      append_utf8(out, replacement_character);
    } else {
      append_utf8(out, unit);
    }
  }
  if (i < bytes.size()) append_utf8(out, replacement_character);  // dangling odd byte
  return out;
}

}  // namespace minedict::unicode
