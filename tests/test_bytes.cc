#include <array>

#include "check.hpp"
#include "minedict/bytes.hpp"

using namespace minedict;

namespace {
constexpr std::array<std::byte, 8> kSample{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
                                           std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08}};
}  // namespace

TEST(bytes, big_and_little_endian_readers) {
  CHECK_EQ(read_be(ByteView{kSample}.first(4)), 0x01020304U);
  CHECK_EQ(read_le(ByteView{kSample}.first(4)), 0x04030201U);
  CHECK_EQ(read_be(kSample), 0x0102030405060708ULL);
  CHECK_EQ(read_be(ByteView{kSample}.first(1)), 1U);
  CHECK_EQ(read_be(ByteView{}), 0U);
}

TEST(bytes, reader_walks_fields_in_order) {
  Reader reader{kSample};
  CHECK_EQ(reader.u8(), 0x01U);
  CHECK_EQ(reader.u16(), 0x0203U);
  CHECK_EQ(reader.position(), 3U);
  const ByteView rest = reader.take(5);
  CHECK_EQ(rest.size(), 5U);
  CHECK(reader.done());
  CHECK(reader.ok());
  CHECK(reader.status().has_value());
}

TEST(bytes, reader_error_is_sticky_and_reads_zero) {
  Reader reader{ByteView{kSample}.first(3)};
  CHECK_EQ(reader.u16(), 0x0102U);
  CHECK_EQ(reader.u32(), 0U);  // only one byte left
  CHECK_FALSE(reader.ok());
  CHECK_EQ(reader.u8(), 0U);  // still refuses, even though one byte would fit
  const auto status = reader.status();
  REQUIRE(!status.has_value());
  CHECK_EQ(status.error().kind, ErrorKind::format);
  CHECK(status.error().message.contains("truncated"));
}

TEST(bytes, reader_number_width_and_little_endian) {
  Reader reader{kSample};
  CHECK_EQ(reader.number(NumberWidth::four), 0x01020304U);
  CHECK_EQ(reader.u32_le(), 0x08070605U);
  CHECK(reader.done());
}

TEST(bytes, text_views_round_trip) {
  const std::string_view text = "abc";
  const ByteView bytes = as_bytes(text);
  CHECK_EQ(bytes.size(), 3U);
  CHECK_EQ(as_chars(bytes), text);
}
