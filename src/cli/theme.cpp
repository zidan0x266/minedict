// minedict - BSD 3-Clause License; see README.
#include "minedict/cli/theme.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <ranges>
#include <string>
#include <utility>

#include "minedict/cli/text.hpp"

namespace minedict::cli {
namespace {

struct NamedColor {
  std::string_view name;
  std::uint32_t hex;
};

// The CSS colour names dictionaries actually use, plus the usual suspects.
constexpr std::array kNamedColors{
    NamedColor{"black", 0x000000},       NamedColor{"white", 0xFFFFFF},
    NamedColor{"red", 0xFF0000},         NamedColor{"darkred", 0x8B0000},
    NamedColor{"maroon", 0x800000},      NamedColor{"crimson", 0xDC143C},
    NamedColor{"firebrick", 0xB22222},   NamedColor{"brown", 0xA52A2A},
    NamedColor{"saddlebrown", 0x8B4513}, NamedColor{"sienna", 0xA0522D},
    NamedColor{"chocolate", 0xD2691E},   NamedColor{"peru", 0xCD853F},
    NamedColor{"orange", 0xFFA500},      NamedColor{"darkorange", 0xFF8C00},
    NamedColor{"orangered", 0xFF4500},   NamedColor{"coral", 0xFF7F50},
    NamedColor{"tomato", 0xFF6347},      NamedColor{"salmon", 0xFA8072},
    NamedColor{"gold", 0xFFD700},        NamedColor{"goldenrod", 0xDAA520},
    NamedColor{"yellow", 0xFFFF00},      NamedColor{"khaki", 0xF0E68C},
    NamedColor{"olive", 0x808000},       NamedColor{"green", 0x008000},
    NamedColor{"darkgreen", 0x006400},   NamedColor{"lime", 0x00FF00},
    NamedColor{"limegreen", 0x32CD32},   NamedColor{"forestgreen", 0x228B22},
    NamedColor{"seagreen", 0x2E8B57},    NamedColor{"darkolivegreen", 0x556B2F},
    NamedColor{"teal", 0x008080},        NamedColor{"cyan", 0x00FFFF},
    NamedColor{"aqua", 0x00FFFF},        NamedColor{"turquoise", 0x40E0D0},
    NamedColor{"darkcyan", 0x008B8B},    NamedColor{"lightblue", 0xADD8E6},
    NamedColor{"skyblue", 0x87CEEB},     NamedColor{"deepskyblue", 0x00BFFF},
    NamedColor{"dodgerblue", 0x1E90FF},  NamedColor{"steelblue", 0x4682B4},
    NamedColor{"royalblue", 0x4169E1},   NamedColor{"cornflowerblue", 0x6495ED},
    NamedColor{"blue", 0x0000FF},        NamedColor{"mediumblue", 0x0000CD},
    NamedColor{"darkblue", 0x00008B},    NamedColor{"navy", 0x000080},
    NamedColor{"midnightblue", 0x191970}, NamedColor{"indigo", 0x4B0082},
    NamedColor{"purple", 0x800080},      NamedColor{"darkviolet", 0x9400D3},
    NamedColor{"blueviolet", 0x8A2BE2},  NamedColor{"violet", 0xEE82EE},
    NamedColor{"darkmagenta", 0x8B008B}, NamedColor{"magenta", 0xFF00FF},
    NamedColor{"fuchsia", 0xFF00FF},     NamedColor{"orchid", 0xDA70D6},
    NamedColor{"plum", 0xDDA0DD},        NamedColor{"pink", 0xFFC0CB},
    NamedColor{"hotpink", 0xFF69B4},     NamedColor{"deeppink", 0xFF1493},
    NamedColor{"gray", 0x808080},        NamedColor{"grey", 0x808080},
    NamedColor{"darkgray", 0xA9A9A9},    NamedColor{"darkgrey", 0xA9A9A9},
    NamedColor{"dimgray", 0x696969},     NamedColor{"dimgrey", 0x696969},
    NamedColor{"lightgray", 0xD3D3D3},   NamedColor{"lightgrey", 0xD3D3D3},
    NamedColor{"slategray", 0x708090},   NamedColor{"slategrey", 0x708090},
    NamedColor{"silver", 0xC0C0C0},      NamedColor{"gainsboro", 0xDCDCDC},
    NamedColor{"whitesmoke", 0xF5F5F5},  NamedColor{"beige", 0xF5F5DC},
    NamedColor{"wheat", 0xF5DEB3},       NamedColor{"tan", 0xD2B48C},
};

struct Hsl {
  double hue;         // degrees, [0, 360)
  double saturation;  // [0, 1]
  double lightness;   // [0, 1]
};

Hsl to_hsl(Rgb colour) noexcept {
  const double r = colour.r / 255.0;
  const double g = colour.g / 255.0;
  const double b = colour.b / 255.0;
  const double max = std::max({r, g, b});
  const double min = std::min({r, g, b});
  const double lightness = (max + min) / 2.0;
  if (max == min) {
    return Hsl{.hue = 0.0, .saturation = 0.0, .lightness = lightness};
  }
  const double delta = max - min;
  const double saturation =
      lightness > 0.5 ? delta / (2.0 - max - min) : delta / (max + min);
  double hue = 0.0;
  if (max == r) {
    hue = (g - b) / delta + (g < b ? 6.0 : 0.0);
  } else if (max == g) {
    hue = (b - r) / delta + 2.0;
  } else {
    hue = (r - g) / delta + 4.0;
  }
  return Hsl{.hue = hue * 60.0, .saturation = saturation, .lightness = lightness};
}

std::optional<int> hex_nibble(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return std::nullopt;
}

std::optional<Rgb> parse_hex_color(std::string_view digits) {
  if (digits.size() == 3) {
    std::array<int, 3> v{};
    for (std::size_t i = 0; i < 3; ++i) {
      const auto n = hex_nibble(digits[i]);
      if (!n) return std::nullopt;
      v[i] = *n * 17;
    }
    return Rgb{.r = static_cast<std::uint8_t>(v[0]), .g = static_cast<std::uint8_t>(v[1]),
               .b = static_cast<std::uint8_t>(v[2])};
  }
  if (digits.size() == 6) {
    std::array<int, 3> v{};
    for (std::size_t i = 0; i < 3; ++i) {
      const auto hi = hex_nibble(digits[2 * i]);
      const auto lo = hex_nibble(digits[2 * i + 1]);
      if (!hi || !lo) return std::nullopt;
      v[i] = *hi * 16 + *lo;
    }
    return Rgb{.r = static_cast<std::uint8_t>(v[0]), .g = static_cast<std::uint8_t>(v[1]),
               .b = static_cast<std::uint8_t>(v[2])};
  }
  return std::nullopt;
}

std::optional<Rgb> parse_rgb_function(std::string_view body) {
  std::array<int, 3> v{};
  std::size_t index = 0;
  std::size_t pos = 0;
  while (index < 3 && pos < body.size()) {
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == ',')) ++pos;
    int value = 0;
    const auto [ptr, ec] = std::from_chars(body.data() + pos, body.data() + body.size(), value);
    if (ec != std::errc{}) return std::nullopt;
    pos = static_cast<std::size_t>(ptr - body.data());
    if (pos < body.size() && body[pos] == '%') {
      value = value * 255 / 100;
      ++pos;
    }
    v[index++] = std::clamp(value, 0, 255);
  }
  if (index != 3) return std::nullopt;
  return Rgb{.r = static_cast<std::uint8_t>(v[0]), .g = static_cast<std::uint8_t>(v[1]),
             .b = static_cast<std::uint8_t>(v[2])};
}

}  // namespace

std::optional<Rgb> parse_css_color(std::string_view css_color) {
  std::string value = text::to_lower_ascii(text::trim(css_color));
  if (value.empty()) return std::nullopt;

  if (value.front() == '#') {
    return parse_hex_color(std::string_view{value}.substr(1));
  }
  if (value.starts_with("rgb(") && value.ends_with(')')) {
    return parse_rgb_function(std::string_view{value}.substr(4, value.size() - 5));
  }
  if (const auto it = std::ranges::find(kNamedColors, value, &NamedColor::name);
      it != kNamedColors.end()) {
    return Rgb::from_hex(it->hex);
  }
  // Some dictionaries write bare hex digits without the hash.
  if (value.size() == 6 || value.size() == 3) {
    return parse_hex_color(value);
  }
  return std::nullopt;
}

Theme::Theme(std::string name, Palette palette, bool colorful)
    : name_(std::move(name)), palette_(palette), colorful_(colorful) {}

Theme Theme::dracula() {
  const Palette& p = palettes::dracula;
  Theme theme{"dracula", p, true};
  // Body text keeps the terminal's own foreground so it reads well on any
  // background; the Dracula accents carry the meaning.
  theme.set(Role::body, Style{});
  theme.set(Role::headword, Style{}.with_fg(p.pink).with_bold());
  theme.set(Role::dictionary_title, Style{}.with_fg(p.comment).with_italic());
  theme.set(Role::banner, Style{}.with_bg(p.current_line).with_fg(p.foreground));
  theme.set(Role::strong, Style{}.with_bold());
  theme.set(Role::emphasis, Style{}.with_italic());
  theme.set(Role::heading, Style{}.with_fg(p.purple).with_bold());
  theme.set(Role::link, Style{}.with_fg(p.cyan).with_underline());
  theme.set(Role::resource, Style{}.with_fg(p.comment));
  theme.set(Role::code, Style{}.with_fg(p.green));
  theme.set(Role::muted, Style{}.with_fg(p.comment));
  theme.set(Role::rule, Style{}.with_fg(p.comment));
  theme.set(Role::bullet, Style{}.with_fg(p.purple));
  theme.set(Role::table_border, Style{}.with_fg(p.comment));
  theme.set(Role::highlight, Style{}.with_fg(p.yellow).with_bold());
  theme.set(Role::label, Style{}.with_fg(p.orange));
  theme.set(Role::key, Style{}.with_fg(p.purple));
  theme.set(Role::value, Style{});
  theme.set(Role::prompt, Style{}.with_fg(p.pink).with_bold());
  theme.set(Role::prompt_hint, Style{}.with_fg(p.comment));
  theme.set(Role::error, Style{}.with_fg(p.red).with_bold());
  theme.set(Role::warning, Style{}.with_fg(p.orange));
  theme.set(Role::success, Style{}.with_fg(p.green));
  return theme;
}

Theme Theme::plain() {
  Theme theme{"plain", palettes::dracula, false};
  theme.set(Role::headword, Style{}.with_bold());
  theme.set(Role::dictionary_title, Style{}.with_italic());
  theme.set(Role::strong, Style{}.with_bold());
  theme.set(Role::emphasis, Style{}.with_italic());
  theme.set(Role::heading, Style{}.with_bold());
  theme.set(Role::link, Style{}.with_underline());
  theme.set(Role::resource, Style{}.with_dim());
  theme.set(Role::muted, Style{}.with_dim());
  theme.set(Role::rule, Style{}.with_dim());
  theme.set(Role::table_border, Style{}.with_dim());
  theme.set(Role::highlight, Style{}.with_bold());
  theme.set(Role::key, Style{}.with_bold());
  theme.set(Role::prompt, Style{}.with_bold());
  theme.set(Role::prompt_hint, Style{}.with_dim());
  theme.set(Role::error, Style{}.with_bold());
  return theme;
}

std::optional<Theme> Theme::named(std::string_view name) {
  const std::string lowered = text::to_lower_ascii(text::trim(name));
  if (lowered == "dracula") return dracula();
  if (lowered == "plain" || lowered == "mono" || lowered == "none") return plain();
  return std::nullopt;
}

std::optional<Rgb> Theme::map_html_color(std::string_view css_color) const {
  if (!colorful_) return std::nullopt;
  const auto parsed = parse_css_color(css_color);
  if (!parsed) return std::nullopt;

  const auto [hue, saturation, lightness] = to_hsl(*parsed);
  const Palette& p = palette_;

  if (saturation < 0.2) {
    // Greys. Black and white mean "default text"; everything between is muted.
    if (lightness < 0.15 || lightness > 0.85) return std::nullopt;
    return p.comment;
  }
  if (hue < 15.0 || hue >= 345.0) {
    // Dark, dull reds are browns: dictionaries use them for register labels.
    if (lightness < 0.45 && saturation < 0.75) return p.orange;
    return p.red;
  }
  if (hue < 45.0) return p.orange;
  if (hue < 70.0) return p.yellow;
  if (hue < 170.0) return p.green;
  if (hue < 200.0) return p.cyan;
  if (hue < 250.0) return lightness < 0.35 ? p.purple : p.cyan;
  if (hue < 295.0) return p.purple;
  // Magenta territory: dark shades (CSS purple, darkmagenta) read as purple,
  // bright ones (magenta, hot pink) as pink.
  return lightness < 0.35 ? p.purple : p.pink;
}

}  // namespace minedict::cli
