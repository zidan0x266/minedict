// minedict - BSD 3-Clause License; see README.
//
// Terminal text utilities: display width, ANSI-aware word wrapping and
// HTML entity decoding. Code point primitives live in minedict/unicode.hpp,
// headword comparison in minedict/keys.hpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "minedict/keys.hpp"
#include "minedict/unicode.hpp"

namespace minedict::cli::text {

/// Terminal columns a code point occupies: 0 (combining/zero-width),
/// 1, or 2 (East Asian wide, emoji).
[[nodiscard]] int column_width(char32_t code_point) noexcept;

/// Columns a string occupies; ANSI escape sequences count as zero.
[[nodiscard]] std::size_t display_width(std::string_view s) noexcept;

[[nodiscard]] std::string strip_ansi(std::string_view s);

/// Word-wrap `text` to `width` columns. Embedded '\n' forces a break.
/// SGR sequences are carried across line breaks so styles survive wrapping.
[[nodiscard]] std::vector<std::string> wrap(std::string_view text, std::size_t width,
                                            std::string_view first_indent = {},
                                            std::string_view rest_indent = {});

/// Cut `s` to at most `width` columns, appending `ellipsis` when cut.
[[nodiscard]] std::string truncate(std::string_view s, std::size_t width,
                                   std::string_view ellipsis = "…");

[[nodiscard]] std::string_view trim(std::string_view s) noexcept;
[[nodiscard]] std::string to_lower_ascii(std::string_view s);
[[nodiscard]] bool iequals_ascii(std::string_view a, std::string_view b) noexcept;

/// Replace HTML character references (`&amp;`, `&#x4e2d;`, ...) with text.
[[nodiscard]] std::string decode_entities(std::string_view s);

/// Map digits and common signs to their Unicode super/subscript forms.
[[nodiscard]] std::string superscript(std::string_view s);
[[nodiscard]] std::string subscript(std::string_view s);

[[nodiscard]] std::string repeat(std::string_view piece, std::size_t count);
[[nodiscard]] std::string pad_right(std::string_view s, std::size_t width);
[[nodiscard]] std::string with_thousands(std::uint64_t value);
[[nodiscard]] std::string human_size(std::uint64_t bytes);

}  // namespace minedict::cli::text
