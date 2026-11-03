#include <string>
#include <variant>

#include "check.hpp"
#include "minedict/cli/html.hpp"

using namespace minedict::cli::html;

TEST(html, tokenizes_tags_text_and_comments) {
  const auto tokens = tokenize("<P CLASS=\"Note\" id=x>Hi &amp; bye<br/></p><!-- c -->");
  REQUIRE_EQ(tokens.size(), 5U);
  const auto* open = std::get_if<Tag>(&tokens[0]);
  REQUIRE(open != nullptr);
  CHECK_EQ(open->name, "p");  // names are lower-cased
  CHECK_FALSE(open->closing);
  CHECK_EQ(open->attribute("class").value_or(""), "Note");
  CHECK_EQ(open->attribute("id").value_or(""), "x");  // unquoted values work too
  CHECK_FALSE(open->attribute("missing").has_value());

  const auto* text = std::get_if<Text>(&tokens[1]);
  REQUIRE(text != nullptr);
  CHECK_EQ(text->content, "Hi &amp; bye");  // entities stay encoded in tokens

  const auto* br = std::get_if<Tag>(&tokens[2]);
  REQUIRE(br != nullptr);
  CHECK(br->self_closing);

  const auto* close = std::get_if<Tag>(&tokens[3]);
  REQUIRE(close != nullptr);
  CHECK(close->closing);
  CHECK(std::holds_alternative<Comment>(tokens[4]));
}

TEST(html, attribute_values_are_entity_decoded) {
  const auto tokens = tokenize("<a href=\"x?a=1&amp;b=2\" title='it&#39;s'>");
  REQUIRE_EQ(tokens.size(), 1U);
  const Tag& tag = std::get<Tag>(tokens[0]);
  CHECK_EQ(tag.attribute("href").value_or(""), "x?a=1&b=2");
  CHECK_EQ(tag.attribute("title").value_or(""), "it's");
}

TEST(html, raw_text_elements_are_not_parsed_inside) {
  const auto tokens = tokenize("<style>a > b { x: 1 }</style>after");
  bool saw_after = false;
  bool saw_bogus_tag = false;
  for (const Token& token : tokens) {
    if (const auto* text = std::get_if<Text>(&token)) saw_after = saw_after || text->content == "after";
    if (const auto* tag = std::get_if<Tag>(&token)) saw_bogus_tag = saw_bogus_tag || tag->name == "b";
  }
  CHECK(saw_after);
  CHECK_FALSE(saw_bogus_tag);  // the `>` inside the style text is not markup
}

TEST(html, element_classes) {
  CHECK(is_void_element("br"));
  CHECK(is_void_element("img"));
  CHECK_FALSE(is_void_element("span"));
  CHECK(is_block_element("div"));
  CHECK(is_block_element("li"));
  CHECK_FALSE(is_block_element("b"));
  CHECK(is_raw_text_element("script"));
  CHECK_FALSE(is_raw_text_element("p"));
}
