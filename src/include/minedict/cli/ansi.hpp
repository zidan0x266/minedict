// minedict - BSD 3-Clause License; see README.
//
// Colours, text styles and their SGR (Select Graphic Rendition) encoding.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace minedict::cli {

/// A 24-bit colour.
struct Rgb {
  std::uint8_t r{};
  std::uint8_t g{};
  std::uint8_t b{};

  [[nodiscard]] static constexpr Rgb from_hex(std::uint32_t hex) noexcept {
    return Rgb{.r = static_cast<std::uint8_t>((hex >> 16U) & 0xFFU),
               .g = static_cast<std::uint8_t>((hex >> 8U) & 0xFFU),
               .b = static_cast<std::uint8_t>(hex & 0xFFU)};
  }

  [[nodiscard]] constexpr bool operator==(const Rgb&) const noexcept = default;
};

/// How much colour the output device understands.
enum class ColorMode { none, ansi256, truecolor };

/// A terminal text style: optional colours plus SGR attributes.
struct Style {
  std::optional<Rgb> fg;
  std::optional<Rgb> bg;
  bool bold{false};
  bool dim{false};
  bool italic{false};
  bool underline{false};
  bool strike{false};

  [[nodiscard]] constexpr Style with_fg(Rgb colour) const noexcept {
    Style s = *this;
    s.fg = colour;
    return s;
  }
  [[nodiscard]] constexpr Style with_bg(Rgb colour) const noexcept {
    Style s = *this;
    s.bg = colour;
    return s;
  }
  [[nodiscard]] constexpr Style with_bold(bool on = true) const noexcept {
    Style s = *this;
    s.bold = on;
    return s;
  }
  [[nodiscard]] constexpr Style with_dim(bool on = true) const noexcept {
    Style s = *this;
    s.dim = on;
    return s;
  }
  [[nodiscard]] constexpr Style with_italic(bool on = true) const noexcept {
    Style s = *this;
    s.italic = on;
    return s;
  }
  [[nodiscard]] constexpr Style with_underline(bool on = true) const noexcept {
    Style s = *this;
    s.underline = on;
    return s;
  }
  [[nodiscard]] constexpr Style with_strike(bool on = true) const noexcept {
    Style s = *this;
    s.strike = on;
    return s;
  }

  [[nodiscard]] constexpr bool is_plain() const noexcept {
    return !fg && !bg && !bold && !dim && !italic && !underline && !strike;
  }

  /// Layer `top` over this style: colours in `top` win when present,
  /// attributes accumulate.
  [[nodiscard]] constexpr Style layered(const Style& top) const noexcept {
    Style s = *this;
    if (top.fg) s.fg = top.fg;
    if (top.bg) s.bg = top.bg;
    s.bold = s.bold || top.bold;
    s.dim = s.dim || top.dim;
    s.italic = s.italic || top.italic;
    s.underline = s.underline || top.underline;
    s.strike = s.strike || top.strike;
    return s;
  }

  [[nodiscard]] constexpr bool operator==(const Style&) const noexcept = default;
};

/// Nearest colour in the xterm 256-colour palette.
[[nodiscard]] std::uint8_t to_ansi256(Rgb colour) noexcept;

/// The escape sequence that switches a terminal into `style`
/// (empty when `mode` is none or the style is plain).
[[nodiscard]] std::string sgr(const Style& style, ColorMode mode);

inline constexpr std::string_view sgr_reset{"\x1b[0m"};

/// `text` wrapped in the style's SGR sequence and a reset.
[[nodiscard]] std::string paint(std::string_view text, const Style& style, ColorMode mode);

/// True when `sequence` is an SGR reset (ESC[0m, ESC[m, ...).
[[nodiscard]] bool is_sgr_reset(std::string_view sequence) noexcept;

}  // namespace minedict::cli
