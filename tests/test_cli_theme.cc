#include <array>
#include <string>

#include "check.hpp"
#include "minedict/cli/theme.hpp"

using namespace minedict::cli;

namespace {

bool is_accent(const Theme& theme, Rgb colour) {
  const Palette& p = theme.palette();
  for (const Rgb accent : {p.foreground, p.comment, p.cyan, p.green, p.orange, p.pink, p.purple, p.red, p.yellow}) {
    if (accent == colour) return true;
  }
  return false;
}

}  // namespace

TEST(theme, lookup_by_name) {
  CHECK_EQ(Theme::dracula().name(), "dracula");
  CHECK_EQ(Theme::plain().name(), "plain");
  REQUIRE(Theme::named("Plain").has_value());
  CHECK_FALSE(Theme::named("Plain")->colorful());
  CHECK(Theme::named("DRACULA")->colorful());
  CHECK_FALSE(Theme::named("solarized").has_value());
  CHECK_EQ(Theme::names().size(), 2U);
}

TEST(theme, roles_have_styles) {
  const Theme dracula = Theme::dracula();
  CHECK(dracula[Role::headword].bold);
  CHECK(dracula[Role::headword].fg.has_value());
  CHECK(dracula[Role::strong].bold);
  CHECK(dracula[Role::emphasis].italic);
  CHECK_FALSE(dracula[Role::body].fg.has_value() && !dracula.colorful());

  const Theme plain = Theme::plain();
  CHECK(plain[Role::strong].bold);
  CHECK_FALSE(plain[Role::headword].fg.has_value());
}

TEST(theme, html_colours_map_onto_the_palette) {
  const Theme dracula = Theme::dracula();
  for (const std::string_view css : {"red", "#00008b", "darkblue", "rgb(0, 136, 221)", "#F80AB8", "green", "brown"}) {
    const auto mapped = dracula.map_html_color(css);
    REQUIRE(mapped.has_value());
    CHECK(is_accent(dracula, *mapped));
  }
  CHECK_FALSE(dracula.map_html_color("#000000").has_value());  // near black: leave the terminal colour alone
  CHECK_FALSE(dracula.map_html_color("#ffffff").has_value());
  CHECK_FALSE(dracula.map_html_color("not-a-colour").has_value());
  CHECK_FALSE(Theme::plain().map_html_color("red").has_value());
}

TEST(theme, dracula_palette_values) {
  const Palette& p = palettes::dracula;
  CHECK(p.background == Rgb::from_hex(0x282A36));
  CHECK(p.pink == Rgb::from_hex(0xFF79C6));
  CHECK(p.purple == Rgb::from_hex(0xBD93F9));
  CHECK_EQ(p.cyan.r, 0x8B);
}
