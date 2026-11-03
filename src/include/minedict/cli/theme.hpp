// minedict - BSD 3-Clause License; see README.
//
// Colour themes. The default is Dracula (https://draculatheme.com): every
// colour that a dictionary's HTML asks for is remapped onto the Dracula
// accents, so entries from very different dictionaries look like one family.
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "minedict/cli/ansi.hpp"

namespace minedict::cli {

/// The eleven canonical Dracula swatches.
struct Palette {
  Rgb background;
  Rgb current_line;
  Rgb foreground;
  Rgb comment;
  Rgb cyan;
  Rgb green;
  Rgb orange;
  Rgb pink;
  Rgb purple;
  Rgb red;
  Rgb yellow;
};

namespace palettes {

inline constexpr Palette dracula{
    .background = Rgb::from_hex(0x282A36),
    .current_line = Rgb::from_hex(0x44475A),
    .foreground = Rgb::from_hex(0xF8F8F2),
    .comment = Rgb::from_hex(0x6272A4),
    .cyan = Rgb::from_hex(0x8BE9FD),
    .green = Rgb::from_hex(0x50FA7B),
    .orange = Rgb::from_hex(0xFFB86C),
    .pink = Rgb::from_hex(0xFF79C6),
    .purple = Rgb::from_hex(0xBD93F9),
    .red = Rgb::from_hex(0xFF5555),
    .yellow = Rgb::from_hex(0xF1FA8C),
};

}  // namespace palettes

/// Semantic slots a theme assigns styles to.
enum class Role : std::size_t {
  body,
  headword,
  dictionary_title,
  banner,
  strong,
  emphasis,
  heading,
  link,
  resource,
  code,
  muted,
  rule,
  bullet,
  table_border,
  highlight,
  label,
  key,
  value,
  prompt,
  prompt_hint,
  error,
  warning,
  success,
  count_,
};

class Theme {
 public:
  [[nodiscard]] static Theme dracula();
  [[nodiscard]] static Theme plain();

  /// Look a theme up by its user-facing name (case-insensitive).
  [[nodiscard]] static std::optional<Theme> named(std::string_view name);
  [[nodiscard]] static constexpr std::array<std::string_view, 2> names() noexcept {
    return {"dracula", "plain"};
  }

  [[nodiscard]] std::string_view name() const noexcept { return name_; }
  [[nodiscard]] const Palette& palette() const noexcept { return palette_; }
  [[nodiscard]] bool colorful() const noexcept { return colorful_; }

  [[nodiscard]] const Style& operator[](Role role) const noexcept {
    return styles_[std::to_underlying(role)];
  }

  /// Translate a colour as written in dictionary HTML (`red`, `#00008b`,
  /// `rgb(0,0,139)`, ...) into the theme's closest accent. Returns nothing
  /// when the theme has no colours or the value cannot be parsed.
  [[nodiscard]] std::optional<Rgb> map_html_color(std::string_view css_color) const;

 private:
  Theme(std::string name, Palette palette, bool colorful);
  void set(Role role, Style style) noexcept { styles_[std::to_underlying(role)] = style; }

  std::string name_;
  Palette palette_;
  bool colorful_;
  std::array<Style, std::to_underlying(Role::count_)> styles_{};
};

/// Parse a CSS colour literal: named colours, `#rgb`, `#rrggbb`, `rgb(r,g,b)`.
[[nodiscard]] std::optional<Rgb> parse_css_color(std::string_view css_color);

}  // namespace minedict::cli
