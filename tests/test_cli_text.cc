#include <string>

#include "check.hpp"
#include "minedict/cli/ansi.hpp"
#include "minedict/cli/text.hpp"

using namespace minedict::cli;
using namespace minedict::cli::text;

TEST(text, display_width_counts_columns_not_bytes) {
  CHECK_EQ(display_width("hello"), 5U);
  CHECK_EQ(display_width("中文"), 4U);
  CHECK_EQ(display_width("é"), 1U);  // combining accent
  CHECK_EQ(display_width("\x1b[1;31mred\x1b[0m"), 3U);
}

TEST(text, wrap_breaks_on_spaces_and_keeps_styles) {
  const auto lines = wrap("the quick brown fox jumps", 10);
  REQUIRE_EQ(lines.size(), 3U);
  CHECK_EQ(lines[0], "the quick");
  CHECK_EQ(lines[1], "brown fox");
  CHECK_EQ(lines[2], "jumps");

  const std::string styled = std::string("\x1b[1m") + "bold words here" + "\x1b[0m";
  const auto styled_lines = wrap(styled, 10);
  REQUIRE_EQ(styled_lines.size(), 2U);
  CHECK_EQ(strip_ansi(styled_lines[0]), "bold words");
  CHECK(styled_lines[0].ends_with("\x1b[0m"));
  CHECK(styled_lines[1].starts_with("\x1b[1m"));  // style re-opened
}

TEST(text, wrap_uses_indents_and_hard_breaks) {
  const auto lines = wrap("one two\nthree", 20, "* ", "  ");
  REQUIRE_EQ(lines.size(), 2U);
  CHECK_EQ(lines[0], "* one two");
  CHECK_EQ(lines[1], "  three");
  const auto cjk = wrap("中文测试", 4);
  REQUIRE_EQ(cjk.size(), 2U);
  CHECK_EQ(cjk[0], "中文");
}

TEST(text, wrap_splits_overlong_words) {
  const auto lines = wrap("abcdefghij", 4);
  REQUIRE_EQ(lines.size(), 3U);
  CHECK_EQ(lines[0], "abcd");
  CHECK_EQ(lines[2], "ij");
}

TEST(text, decode_entities_covers_named_numeric_and_cp1252) {
  CHECK_EQ(decode_entities("a &amp; b &lt;c&gt; &quot;d&quot;"), "a & b <c> \"d\"");
  CHECK_EQ(decode_entities("&#20013;&#x6587;"), "中文");
  CHECK_EQ(decode_entities("&#146;"), "\xE2\x80\x99");  // right single quotation mark
  CHECK_EQ(decode_entities("&bogus; &"), "&bogus; &");
  CHECK_EQ(decode_entities("x&nbsp;y"), "x\xC2\xA0y");
}

TEST(text, scripts_and_helpers) {
  CHECK_EQ(superscript("12"), "¹²");
  CHECK_EQ(subscript("2"), "₂");
  CHECK_EQ(text::truncate("abcdefgh", 5), "abcd…");
  CHECK_EQ(text::truncate("abc", 5), "abc");
  CHECK_EQ(with_thousands(15773), "15,773");
  CHECK_EQ(with_thousands(999), "999");
  CHECK_EQ(trim("  x\t\n"), "x");
  CHECK_EQ(pad_right("ab", 4), "ab  ");
  CHECK_EQ(to_lower_ascii("MiXeD"), "mixed");
  CHECK(iequals_ascii("Html", "HTML"));
}

TEST(ansi, sgr_and_ansi256) {
  const Style style = Style{}.with_bold().with_fg(Rgb::from_hex(0xFF79C6));
  CHECK_EQ(sgr(style, ColorMode::truecolor), "\x1b[1;38;2;255;121;198m");
  CHECK_EQ(sgr(style, ColorMode::none), "");
  CHECK_EQ(paint("x", style, ColorMode::none), "x");
  CHECK_EQ(to_ansi256(Rgb{0, 0, 0}), 16);
  CHECK_EQ(to_ansi256(Rgb{255, 255, 255}), 231);
  CHECK_EQ(to_ansi256(Rgb{128, 128, 128}), 244);
  const Style layered = Style{}.with_italic().layered(Style{}.with_fg(Rgb{1, 2, 3}).with_bold());
  CHECK(layered.italic && layered.bold);
  CHECK((layered.fg == Rgb{1, 2, 3}));
}
