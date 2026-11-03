// minedict - BSD 3-Clause License; see README.
#include "minedict/cli/ansi.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>

namespace minedict::cli {
namespace {

constexpr std::array<int, 6> kCubeLevels{0, 95, 135, 175, 215, 255};

constexpr int nearest_cube_index(int value) noexcept {
  int best = 0;
  int best_distance = 1 << 30;
  for (int i = 0; i < static_cast<int>(kCubeLevels.size()); ++i) {
    const int distance = std::abs(kCubeLevels[static_cast<std::size_t>(i)] - value);
    if (distance < best_distance) {
      best_distance = distance;
      best = i;
    }
  }
  return best;
}

constexpr int squared_distance(int r1, int g1, int b1, int r2, int g2, int b2) noexcept {
  const int dr = r1 - r2;
  const int dg = g1 - g2;
  const int db = b1 - b2;
  return dr * dr + dg * dg + db * db;
}

}  // namespace

std::uint8_t to_ansi256(Rgb colour) noexcept {
  const int r = colour.r;
  const int g = colour.g;
  const int b = colour.b;

  const int ri = nearest_cube_index(r);
  const int gi = nearest_cube_index(g);
  const int bi = nearest_cube_index(b);
  const int cube_distance =
      squared_distance(r, g, b, kCubeLevels[static_cast<std::size_t>(ri)],
                       kCubeLevels[static_cast<std::size_t>(gi)],
                       kCubeLevels[static_cast<std::size_t>(bi)]);

  const int grey = (r + g + b) / 3;
  const int grey_index = std::clamp((grey - 8 + 5) / 10, 0, 23);
  const int grey_value = 8 + grey_index * 10;
  const int grey_distance = squared_distance(r, g, b, grey_value, grey_value, grey_value);

  if (grey_distance < cube_distance) {
    return static_cast<std::uint8_t>(232 + grey_index);
  }
  return static_cast<std::uint8_t>(16 + 36 * ri + 6 * gi + bi);
}

std::string sgr(const Style& style, ColorMode mode) {
  if (mode == ColorMode::none || style.is_plain()) {
    return {};
  }
  std::string out{"\x1b["};
  const auto add = [&out](std::string_view parameter) {
    if (out.size() > 2) out += ';';
    out += parameter;
  };
  if (style.bold) add("1");
  if (style.dim) add("2");
  if (style.italic) add("3");
  if (style.underline) add("4");
  if (style.strike) add("9");

  const auto add_colour = [&](const Rgb& colour, int base) {
    if (mode == ColorMode::truecolor) {
      add(std::format("{};2;{};{};{}", base, static_cast<int>(colour.r),
                      static_cast<int>(colour.g), static_cast<int>(colour.b)));
    } else {
      add(std::format("{};5;{}", base, static_cast<int>(to_ansi256(colour))));
    }
  };
  if (style.fg) add_colour(*style.fg, 38);
  if (style.bg) add_colour(*style.bg, 48);

  out += 'm';
  return out;
}

std::string paint(std::string_view text, const Style& style, ColorMode mode) {
  const std::string open = sgr(style, mode);
  if (open.empty() || text.empty()) {
    return std::string(text);
  }
  std::string out;
  out.reserve(open.size() + text.size() + sgr_reset.size());
  out += open;
  out += text;
  out += sgr_reset;
  return out;
}

bool is_sgr_reset(std::string_view sequence) noexcept {
  return sequence == "\x1b[0m" || sequence == "\x1b[m" || sequence == "\x1b[00m";
}

}  // namespace minedict::cli
