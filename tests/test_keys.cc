#include "check.hpp"
#include "minedict/keys.hpp"

using namespace minedict;

TEST(keys, normalize_key_folds_case_and_strips_punctuation) {
  CHECK_EQ(normalize_key("Ice-Cream!"), "icecream");
  CHECK_EQ(normalize_key("ÉCOLE"), "école");
  CHECK_EQ(normalize_key("a  b\tc"), "abc");
  CHECK_EQ(normalize_key(std::string("nul\0byte", 8)), "nulbyte");
  CHECK_EQ(normalize_key("Ice-Cream", KeyRules{.case_sensitive = true, .strip_punctuation = false}), "Ice-Cream");
  CHECK_EQ(normalize_key("\\pic\\A.png", KeyRules{.case_sensitive = false, .strip_punctuation = false}), "\\pic\\a.png");
  CHECK_EQ(normalize_key("中文、測試"), "中文測試");
}

TEST(keys, edit_distance_is_optimal_string_alignment) {
  CHECK_EQ(edit_distance("kitten", "sitting", 5), 3U);
  CHECK_EQ(edit_distance("abcd", "abdc", 2), 1U);  // transposition counts once
  CHECK_EQ(edit_distance("same", "same", 2), 0U);
  CHECK_EQ(edit_distance("", "abc", 5), 3U);
  CHECK_EQ(edit_distance("短", "短い", 2), 1U);  // code points, not bytes
  CHECK_EQ(edit_distance("short", "a much longer string", 2), 3U);  // capped at cap + 1
}
