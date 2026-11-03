#include <vector>

#include "check.hpp"
#include "minedict/checksum.hpp"

using namespace minedict;

TEST(checksum, adler32_known_values) {
  CHECK_EQ(adler32(ByteView{}), 1U);
  CHECK_EQ(adler32(as_bytes("Wikipedia")), 0x11E60398U);
  CHECK_EQ(adler32(as_bytes("a")), 0x00620062U);
}

TEST(checksum, adler32_is_chainable_across_chunks) {
  std::vector<std::byte> data(70'000);
  for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::byte>((i * 31 + 7) & 0xFF);
  const ByteView all{data};
  CHECK_EQ(adler32(all), adler32(all.subspan(12'345), adler32(all.first(12'345))));
}

TEST(checksum, adler32_is_constexpr) {
  constexpr std::array<std::byte, 3> abc{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
  constexpr std::uint32_t value = adler32(abc);
  static_assert(value == 0x024D0127U);
  CHECK_EQ(value, 0x024D0127U);
}
