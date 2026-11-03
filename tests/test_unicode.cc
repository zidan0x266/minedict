#include <array>
#include <string>

#include "check.hpp"
#include "minedict/unicode.hpp"

using namespace minedict;
using namespace minedict::unicode;

TEST(unicode, decode_utf8_handles_all_lengths) {
  CHECK_EQ(decode_utf8("a", 0).code_point, U'a');
  CHECK_EQ(decode_utf8("é", 0).size, 2U);
  CHECK_EQ(decode_utf8("中", 0).code_point, 0x4E2DU);
  CHECK_EQ(decode_utf8("中", 0).size, 3U);
  CHECK_EQ(decode_utf8("\xF0\x9F\x98\x80", 0).size, 4U);  // U+1F600
  const auto invalid = decode_utf8("\xFF", 0);
  CHECK_EQ(invalid.code_point, replacement_character);
  CHECK_EQ(invalid.size, 1U);
  CHECK_EQ(decode_utf8("\xC0\x80", 0).code_point, replacement_character);  // overlong
  CHECK_EQ(decode_utf8("abc", 3).code_point, replacement_character);      // past the end
}

TEST(unicode, encode_and_round_trip) {
  CHECK_EQ(encode_utf8(U'a'), "a");
  CHECK_EQ(encode_utf8(0xE9), "é");
  CHECK_EQ(encode_utf8(0x4E2D), "中");
  CHECK_EQ(encode_utf8(0x1F600), "\xF0\x9F\x98\x80");
  CHECK_EQ(encode_utf8(0xD800), encode_utf8(replacement_character));  // surrogates are not scalar values
  const std::string text = "naïve 中文 \xF0\x9F\x98\x80";
  CHECK_EQ(from_u32(to_u32(text)), text);
  CHECK_EQ(to_u32("中文").size(), 2U);
}

TEST(unicode, utf16le_to_utf8_covers_bmp_pairs_and_garbage) {
  constexpr std::array<std::byte, 4> hi{std::byte{0x68}, std::byte{0x00}, std::byte{0x2D}, std::byte{0x4E}};
  CHECK_EQ(utf16le_to_utf8(hi), "h中");
  constexpr std::array<std::byte, 4> grin{std::byte{0x3D}, std::byte{0xD8}, std::byte{0x00}, std::byte{0xDE}};
  CHECK_EQ(utf16le_to_utf8(grin), "\xF0\x9F\x98\x80");
  constexpr std::array<std::byte, 3> odd{std::byte{0x61}, std::byte{0x00}, std::byte{0x62}};
  CHECK_EQ(utf16le_to_utf8(odd), "a" + encode_utf8(replacement_character));
  constexpr std::array<std::byte, 4> lone{std::byte{0x00}, std::byte{0xDC}, std::byte{0x61}, std::byte{0x00}};
  CHECK_EQ(utf16le_to_utf8(lone), encode_utf8(replacement_character) + "a");
}

TEST(unicode, character_classes) {
  CHECK(is_space(U' '));
  CHECK(is_space(0x00A0));
  CHECK(is_space(0x2002));  // en space
  CHECK(is_space(0x3000));
  CHECK_FALSE(is_space(U'a'));
  CHECK(is_punctuation(U'-'));
  CHECK(is_punctuation(0x2019));  // right single quote
  CHECK(is_punctuation(0x3002));  // ideographic full stop
  CHECK_FALSE(is_punctuation(U'z'));
  CHECK_FALSE(is_punctuation(0x4E2D));
}

TEST(unicode, fold_case_is_simple_and_wide) {
  CHECK_EQ(fold_case(U'A'), U'a');
  CHECK_EQ(fold_case(U'z'), U'z');
  CHECK_EQ(fold_case(0xC9), 0xE9U);    // É -> é
  CHECK_EQ(fold_case(0x178), 0xFFU);   // Ÿ -> ÿ
  CHECK_EQ(fold_case(0x391), 0x3B1U);  // Α -> α
  CHECK_EQ(fold_case(0x416), 0x436U);  // Ж -> ж
  CHECK_EQ(fold_case(0xFF21), 0xFF41U);
  CHECK_EQ(fold_case(0x4E2D), 0x4E2DU);
  static_assert(fold_case(U'Q') == U'q');
}
