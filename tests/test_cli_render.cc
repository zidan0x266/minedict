#include <string>
#include <vector>

#include "check.hpp"
#include "minedict/cli/css.hpp"
#include "minedict/cli/render.hpp"
#include "minedict/cli/text.hpp"
#include "minedict/cli/theme.hpp"

using namespace minedict::cli;

namespace {

std::string render(std::string_view html, RenderOptions options = {}, const css::Sheet* sheet = nullptr) {
  static const Theme theme = Theme::plain();
  if (sheet != nullptr) options.stylesheets.push_back(sheet);
  return HtmlRenderer{theme, options}.render(html);
}

std::vector<std::string> lines_of(const std::string& text) {
  std::vector<std::string> lines;
  for (std::size_t start = 0; start <= text.size();) {
    const std::size_t end = text.find('\n', start);
    lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return lines;
}

}  // namespace

TEST(render, inline_text_breaks_and_entities) {
  CHECK_EQ(render("plain <b>bold</b> text"), "plain bold text");
  CHECK_EQ(render("one<br>two"), "one\ntwo");
  CHECK_EQ(render("fish &amp; chips &lt;3"), "fish & chips <3");
  CHECK_EQ(render("a   lot    of   space"), "a lot of space");
}

TEST(render, block_elements_start_new_lines) {
  const auto lines = lines_of(render("<div>first</div><p>second</p>third"));
  REQUIRE(lines.size() >= 3U);
  CHECK_EQ(lines[0], "first");
  CHECK(lines[1] == "second" || (lines[1].empty() && lines[2] == "second"));
  CHECK_EQ(lines.back(), "third");
}

TEST(render, lists_tables_and_scripts) {
  const std::string list = render("<ul><li>apple</li><li>pear</li></ul>");
  CHECK(list.contains("apple"));
  CHECK(list.contains("pear"));
  CHECK_EQ(lines_of(list).size(), 2U);

  const std::string table = render("<table><tr><td>a1</td><td>a2</td></tr><tr><td>b1</td><td>&nbsp;</td></tr></table>");
  const auto rows = lines_of(table);
  REQUIRE(rows.size() >= 2U);
  CHECK(rows[0].contains("a1") && rows[0].contains("a2"));
  CHECK(rows[1].contains("b1"));

  CHECK_EQ(render("x<sup>2</sup> + H<sub>2</sub>O"), "x² + H₂O");
  CHECK_EQ(render("<style>b{}</style><script>1</script>visible"), "visible");
}

TEST(render, images_sounds_and_wrapping) {
  CHECK(render("<img src=\"pic.png\">").contains("pic.png"));
  CHECK_EQ(render("<img src=\"pic.png\">", {.show_images = false}), "");
  const std::string sound = render("<a href=\"sound://a.mp3\">play</a>", {.show_sounds = true});
  CHECK(sound.contains("♪"));
  CHECK_FALSE(render("<a href=\"sound://a.mp3\">play</a>", {.show_sounds = false}).contains("♪"));

  const auto wrapped = lines_of(render("the quick brown fox jumps over the lazy dog", {.width = 16}));
  CHECK(wrapped.size() >= 3U);
  for (const std::string& line : wrapped) CHECK(text::display_width(line) <= 16U);

  const auto indented = lines_of(render("hello\nworld", {.width = 40, .indent = 2}));
  for (const std::string& line : indented) CHECK(line.starts_with("  "));
}

TEST(render, stylesheet_drives_layout) {
  const css::Sheet sheet = css::Sheet::parse(R"(
    sense { display: block; counter-increment: n }
    sense::before { content: counter(n) ". " }
    entry { counter-reset: n }
    hidden { display: none }
    label { display: inline-block }
    gap { display: block; margin-top: 1em }
  )");
  const auto lines = lines_of(render("<entry><sense>first</sense><sense>second<hidden>secret</hidden></sense></entry>",
                                     {}, &sheet));
  REQUIRE_EQ(lines.size(), 2U);
  CHECK_EQ(lines[0], "1. first");
  CHECK_EQ(lines[1], "2. second");

  CHECK_EQ(render("<label>n</label>noun", {}, &sheet), "n noun");  // inline-block gets a space
  const auto spaced = lines_of(render("<gap>a</gap><gap>b</gap>", {}, &sheet));
  REQUIRE_EQ(spaced.size(), 3U);
  CHECK(spaced[1].empty());  // a vertical margin becomes a blank line
}

TEST(render, plain_text_helper) {
  CHECK_EQ(html_to_plain_text("<b>Oxford</b> &amp; Co<br>line"), "Oxford & Co\nline");
}
