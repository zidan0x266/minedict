#include <string>

#include "check.hpp"
#include "minedict/ripemd128.hpp"

using namespace minedict;

namespace {
std::string digest_hex(std::string_view message) { return to_hex(ripemd128(as_bytes(message))); }
}  // namespace

TEST(ripemd128, reference_vectors) {
  CHECK_EQ(digest_hex(""), "cdf26213a150dc3ecb610f18f6b38b46");
  CHECK_EQ(digest_hex("a"), "86be7afa339d0fc7cfc785e72f578d33");
  CHECK_EQ(digest_hex("abc"), "c14a12199c66e4ba84636b0f69144c77");
  CHECK_EQ(digest_hex("message digest"), "9e327b3d6e523062afc1132d7df9d1b8");
  CHECK_EQ(digest_hex("abcdefghijklmnopqrstuvwxyz"), "fd2aa607f71dc8f510714922b371834e");
  CHECK_EQ(digest_hex("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"),
           "d1e959eb179c911faea4624c60c5c702");
  CHECK_EQ(digest_hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890"),
           "3f45ef194732c2dbb2c4a2c769795fa3");
}

TEST(ripemd128, million_a) { CHECK_EQ(digest_hex(std::string(1'000'000, 'a')), "4a7f5723f954eba1216c9d8f6320431f"); }

TEST(ripemd128, block_boundaries_differ) {
  CHECK_EQ(digest_hex(std::string(55, 'x')).size(), 32U);
  CHECK_NE(digest_hex(std::string(56, 'x')), digest_hex(std::string(55, 'x')));
  CHECK_NE(digest_hex(std::string(64, 'x')), digest_hex(std::string(65, 'x')));
}
