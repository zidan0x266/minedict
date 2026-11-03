#include <string>

#include "check.hpp"
#include "minedict/header.hpp"
#include "minedict/unicode.hpp"

using namespace minedict;

namespace {

Bytes utf16le(std::string_view utf8) {
  Bytes out;
  for (const char32_t cp : unicode::to_u32(utf8)) {
    const auto put = [&](char32_t unit) {
      out.push_back(static_cast<std::byte>(unit & 0xFF));
      out.push_back(static_cast<std::byte>((unit >> 8) & 0xFF));
    };
    if (cp >= 0x10000) {
      put(0xD800 + ((cp - 0x10000) >> 10));
      put(0xDC00 + ((cp - 0x10000) & 0x3FF));
    } else {
      put(cp);
    }
  }
  return out;
}

}  // namespace

TEST(header, version_parsing_and_ordering) {
  CHECK((Version::parse("2.0") == Version{2, 0}));
  CHECK((Version::parse(" 1.2 ") == Version{1, 2}));
  CHECK((Version::parse("3") == Version{3, 0}));
  CHECK_FALSE(Version::parse("abc").has_value());
  CHECK((Version{1, 2} < Version{2, 0}));
  CHECK_EQ((Version{2, 0}).str(), "2.0");
}

TEST(header, xml_entities) {
  CHECK_EQ(decode_xml_entities("a &lt;b&gt; &amp; &quot;c&quot; &apos;d&apos;"), "a <b> & \"c\" 'd'");
  CHECK_EQ(decode_xml_entities("&#20013;&#x6587;"), "中文");
  CHECK_EQ(decode_xml_entities("&bogus; & &#0;"), "&bogus; & &#0;");
}

TEST(header, attribute_scanner) {
  const auto attributes = parse_attributes(
      "<Dictionary GeneratedByEngineVersion=\"2.0\" Encrypted='2'\r\n Description=\"a &lt;b&gt;\nline two\" "
      "Title=\"\" Compact = \"No\"/>");
  REQUIRE(attributes.has_value());
  CHECK_EQ(attributes->size(), 5U);
  CHECK_EQ(attributes->at("GeneratedByEngineVersion"), "2.0");
  CHECK_EQ(attributes->at("Encrypted"), "2");
  CHECK_EQ(attributes->at("Description"), "a <b>\nline two");
  CHECK_EQ(attributes->at("Title"), "");
  CHECK_EQ(attributes->at("Compact"), "No");

  CHECK_FALSE(parse_attributes("no tag here").has_value());
  CHECK_FALSE(parse_attributes("<Dictionary/>").has_value());
  CHECK_FALSE(parse_attributes("<Dictionary a=\"unterminated/>").has_value());
}

TEST(header, stylesheet_parse_and_apply) {
  const StyleSheet sheet = StyleSheet::parse("1\n<b>\n</b>\r\n2\n<i>\n</i>\n");
  REQUIRE_EQ(sheet.rules.size(), 2U);
  CHECK((sheet.rules.at("1") == StyleSheet::Rule{"<b>", "</b>"}));
  CHECK_EQ(sheet.apply("`1`bold`2`italic plain"), "<b>bold</b><i>italic plain</i>");
  CHECK_EQ(sheet.apply("no markers"), "no markers");
  CHECK_EQ(sheet.apply("`9`unknown"), "unknown");
  CHECK_EQ(sheet.apply("a `not a marker` b"), "a `not a marker` b");
  CHECK_EQ(StyleSheet{}.apply("`1`x"), "`1`x");
  CHECK(StyleSheet::parse("   ").empty());
}

TEST(header, parse_header_from_utf16) {
  const std::string xml =
      "\xEF\xBB\xBF<Dictionary GeneratedByEngineVersion=\"2.0\" RequiredEngineVersion=\"2.0\" Encrypted=\"2\" "
      "Encoding=\"UTF-8\" Format=\"Html\" KeyCaseSensitive=\"No\" StripKey=\"Yes\" Title=\"&lt;b&gt;Test&lt;/b&gt;\" "
      "CreationDate=\"2024-1-1\" StyleSheet=\"1\n<b>\n</b>\"/>\r\n";
  const auto header = parse_header(utf16le(xml), Kind::mdx);
  REQUIRE(header.has_value());
  CHECK_EQ(header->kind, Kind::mdx);
  CHECK((header->version == Version{2, 0}));
  CHECK(header->is_v2());
  CHECK_EQ(header->number_width(), NumberWidth::eight);
  CHECK_EQ(header->encoding.charset, Charset::utf8);
  CHECK(header->encryption.key_index);
  CHECK_FALSE(header->encryption.key_blocks);
  CHECK_FALSE(header->case_sensitive);
  CHECK(header->strip_key);
  CHECK_EQ(header->title, "<b>Test</b>");
  CHECK_EQ(header->creation_date, "2024-1-1");
  CHECK_EQ(header->stylesheet.rules.size(), 1U);
  CHECK_EQ(header->attribute("Format"), "Html");
  CHECK_EQ(header->attribute("Missing"), "");
}

TEST(header, legacy_and_resource_headers) {
  const auto v1 = parse_header(
      utf16le("<Dictionary GeneratedByEngineVersion=\"1.2\" Encrypted=\"No\" Encoding=\"BIG5\" KeyCaseSensitive=\"No\"/>"),
      Kind::mdx);
  REQUIRE(v1.has_value());
  CHECK_FALSE(v1->is_v2());
  CHECK_EQ(v1->number_width(), NumberWidth::four);
  CHECK_EQ(v1->encoding.charset, Charset::big5);
  CHECK_FALSE(static_cast<bool>(v1->encryption));

  const auto mdd = parse_header(
      utf16le("<Library_Data GeneratedByEngineVersion=\"2.0\" Encrypted=\"Yes\" Encoding=\"\" Title=\"\"/>"), Kind::mdd);
  REQUIRE(mdd.has_value());
  CHECK_EQ(mdd->kind, Kind::mdd);
  CHECK_EQ(mdd->encoding.charset, Charset::utf16le);  // forced for resource files
  CHECK(mdd->encryption.key_blocks);

  CHECK_FALSE(parse_header(utf16le("<Dictionary GeneratedByEngineVersion=\"3.0\"/>"), Kind::mdx).has_value());
}

TEST(header, encoding_names) {
  CHECK_EQ(Encoding::from_name("").charset, Charset::utf8);
  CHECK_EQ(Encoding::from_name("utf-8").charset, Charset::utf8);
  CHECK_EQ(Encoding::from_name("UTF-16").charset, Charset::utf16le);
  CHECK_EQ(Encoding::from_name("GBK").charset, Charset::gb18030);
  CHECK_EQ(Encoding::from_name("gb2312").charset, Charset::gb18030);
  CHECK_EQ(Encoding::from_name("Big5").charset, Charset::big5);
  CHECK_EQ(Encoding::from_name("ISO-8859-1").charset, Charset::latin1);
  CHECK_EQ(Encoding::from_name("KOI8-R").charset, Charset::other);
  CHECK_EQ(Encoding::utf16le().unit_size(), 2U);
  CHECK_EQ(Encoding::from_name("Big5").unit_size(), 1U);
}
