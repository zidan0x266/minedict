#include <string>
#include <vector>

#include "check.hpp"
#include "mdx_builder.hpp"
#include "minedict/checksum.hpp"
#include "minedict/compression.hpp"

using namespace minedict;
using namespace minedict::testing;

namespace {

Bytes envelope(Compression compression, ByteView raw, ByteView payload, std::uint32_t checksum_override = 0) {
  Bytes block;
  put_le(block, static_cast<std::uint64_t>(compression), 4);
  put_be(block, checksum_override != 0 ? checksum_override : adler32(raw), 4);
  append(block, payload);
  return block;
}

const std::string kText = "Every block starts with a tag and a checksum. Every block starts with a tag and a checksum.";

}  // namespace

TEST(compression, parse_block_reads_tag_and_checksum) {
  const Bytes block = envelope(Compression::zlib, as_bytes(kText), as_bytes("payload"));
  const auto parsed = parse_block(block);
  REQUIRE(parsed.has_value());
  CHECK_EQ(parsed->compression, Compression::zlib);
  CHECK_EQ(parsed->checksum, adler32(as_bytes(kText)));
  CHECK_EQ(as_chars(parsed->payload), "payload");
  CHECK_EQ(to_string(parsed->compression), "zlib");

  CHECK_FALSE(parse_block(ByteView{block}.first(5)).has_value());
  Bytes bad_tag = block;
  bad_tag[0] = std::byte{7};
  CHECK_EQ(parse_block(bad_tag).error().kind, ErrorKind::format);
}

TEST(compression, zlib_round_trips_both_block_styles) {
  const ByteView raw = as_bytes(kText);
  for (const ZlibStyle style : {ZlibStyle::stored, ZlibStyle::fixed_huffman}) {
    const Bytes block = make_block(raw, Compression::zlib, style);
    const auto data = unpack_block(block, kText.size());
    REQUIRE(data.has_value());
    CHECK_EQ(as_chars(*data), kText);
    CHECK_FALSE(unpack_block(block, kText.size() + 1).has_value());  // declared size must match
  }
}

TEST(compression, stored_blocks_and_checksum_verification) {
  const ByteView raw = as_bytes(kText);
  const Bytes block = envelope(Compression::none, raw, raw);
  CHECK(unpack_block(block, kText.size()).has_value());

  const Bytes wrong = envelope(Compression::none, raw, raw, 0xDEADBEEF);
  const auto rejected = unpack_block(wrong, kText.size());
  REQUIRE(!rejected.has_value());
  CHECK(rejected.error().message.contains("checksum"));
  CHECK(unpack_block(wrong, kText.size(), /*verify_checksum=*/false).has_value());
}
