#include <string>
#include <vector>

#include "check.hpp"
#include "minedict/cli/css.hpp"
#include "minedict/cli/html.hpp"

using namespace minedict::cli;
using namespace minedict::cli::css;

namespace {

Element element(std::string_view tag, std::vector<std::string_view> classes = {}, std::string_view id = {},
                const std::vector<html::Attribute>* attributes = nullptr) {
  return Element{.tag = tag, .classes = std::move(classes), .id = id, .attributes = attributes};
}

using Path = std::vector<Element>;

}  // namespace

TEST(css, resolves_type_class_id_descendant_and_attribute_selectors) {
  const Sheet sheet = Sheet::parse(R"(
    b { font-weight: bold }
    div.note > i, #special { color: red; display: block }
    .hidden { display: none }
    font[color="#FF0000"] { font-style: italic }
    sn-g x { font-style: italic }
  )");
  CHECK_FALSE(sheet.empty());
  CHECK_EQ(sheet.resolve(Path{element("b")}).bold, true);

  const Path child{element("div", {"note"}), element("i")};
  CHECK_EQ(sheet.resolve(child).display, Display::block);
  CHECK_EQ(sheet.resolve(child).color.value_or(""), "red");

  CHECK_EQ(sheet.resolve(Path{element("span", {}, "special")}).display, Display::block);
  CHECK_EQ(sheet.resolve(Path{element("span", {"hidden"})}).display, Display::none);

  const std::vector<html::Attribute> red{{"color", "#FF0000"}};
  CHECK_EQ(sheet.resolve(Path{element("font", {}, {}, &red)}).italic, true);
  const std::vector<html::Attribute> blue{{"color", "#0000FF"}};
  CHECK((sheet.resolve(Path{element("font", {}, {}, &blue)}).empty()));

  CHECK_EQ(sheet.resolve(Path{element("sn-g"), element("span"), element("x")}).italic, true);
  CHECK(sheet.resolve(Path{element("x")}).empty());
}

TEST(css, specificity_and_order_decide_the_cascade) {
  const Sheet sheet = Sheet::parse("b { color: red } .x { color: blue } b { font-weight: bold } b { font-weight: normal }");
  CHECK_EQ(sheet.resolve(Path{element("b", {"x"})}).color.value_or(""), "blue");  // a class beats a type selector
  CHECK_EQ(sheet.resolve(Path{element("b")}).bold, false);                         // the later rule wins
}

TEST(css, maps_properties_to_terminal_notions) {
  const Sheet sheet = Sheet::parse(R"(
    a { display: inline-block; visibility: hidden; text-decoration: underline line-through; white-space: pre }
    c { margin: 0 4px 0 1em; }
    d { padding-left: 0; margin-right: 2px; margin-top: 1em; padding-bottom: 2px }
    e { margin: 0.6em 0 }
    f { font-weight: 700; font-style: oblique }
  )");
  const Declarations a = sheet.resolve(Path{element("a")});
  CHECK_EQ(a.display, Display::inline_block);
  CHECK_EQ(a.hidden, true);
  CHECK_EQ(a.underline, true);
  CHECK_EQ(a.strike, true);
  CHECK_EQ(a.preformatted, true);

  const Declarations c = sheet.resolve(Path{element("c")});
  CHECK_EQ(c.space_before, true);
  CHECK_EQ(c.space_after, true);
  CHECK_FALSE(c.blank_before.has_value());

  const Declarations d = sheet.resolve(Path{element("d")});
  CHECK_FALSE(d.space_before.has_value());
  CHECK_EQ(d.space_after, true);
  CHECK_EQ(d.blank_before, true);          // 1em is at least half a line
  CHECK_FALSE(d.blank_after.has_value());  // 2px is not

  const Declarations e = sheet.resolve(Path{element("e")});
  CHECK_EQ(e.blank_before, true);
  CHECK_EQ(e.blank_after, true);

  const Declarations f = sheet.resolve(Path{element("f")});
  CHECK_EQ(f.bold, true);
  CHECK_EQ(f.italic, true);
}

TEST(css, pseudo_elements_counters_and_escapes) {
  const Sheet sheet = Sheet::parse(R"(
    ol { counter-reset: item }
    li::before { counter-increment: item; content: counter(item) ". "; color: #bd93f9 }
    x:after { content: "\21D2 " }
    q::before, q::after { content: '' }
  )");
  const Path item{element("ol"), element("li")};
  const Declarations before = sheet.resolve(item, Pseudo::before);
  REQUIRE(before.content.has_value());
  CHECK_EQ(before.content->size(), 2U);
  CHECK_EQ(before.counter_increment.size(), 1U);
  CHECK_EQ(before.color.value_or(""), "#bd93f9");
  CHECK_FALSE(sheet.resolve(item).content.has_value());  // the pseudo rule does not leak into the element
  CHECK_EQ(sheet.resolve(Path{element("ol")}).counter_reset.size(), 1U);

  const Declarations after = sheet.resolve(Path{element("x")}, Pseudo::after);
  REQUIRE(after.content.has_value());
  CHECK_EQ(after.content->size(), 1U);

  const Declarations empty = sheet.resolve(Path{element("q")}, Pseudo::before);
  CHECK(!empty.content.has_value() || empty.content->empty());
}

TEST(css, skips_what_a_terminal_cannot_use) {
  const Sheet sheet = Sheet::parse(R"(
    @font-face { font-family: x; src: url(x.ttf) }
    @media print { b { display: none } }
    a:hover { color: red }
    b ~ c { color: red }
    d { color: red; font-family: 'Optima', serif; line-height: 1.5 }
  )");
  CHECK(sheet.resolve(Path{element("b")}).empty());
  CHECK(sheet.resolve(Path{element("a")}).empty());
  CHECK_EQ(sheet.resolve(Path{element("d")}).color.value_or(""), "red");
}
