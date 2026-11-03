#include "minedict/compression.hpp"

#include <algorithm>

#include "minedict/checksum.hpp"
#include "minedict/inflate.hpp"
#include "minedict/lzo.hpp"

namespace minedict {

Result<Block> parse_block(ByteView block) {
  if (block.size() < block_prelude_size) {
    return fail(ErrorKind::format, "block of {} bytes is too short for its 8-byte prelude", block.size());
  }
  const std::uint64_t tag = read_le(block.first(4));
  if (tag > 2) return fail(ErrorKind::format, "unknown block compression tag {:#x}", tag);
  return Block{.compression = static_cast<Compression>(tag),
               .checksum = static_cast<std::uint32_t>(read_be(block.subspan(4, 4))),
               .payload = block.subspan(block_prelude_size)};
}

Result<Bytes> decompress(Compression compression, ByteView payload, std::size_t expected_size) {
  switch (compression) {
    case Compression::none: {
      if (payload.size() != expected_size) {
        return fail(ErrorKind::format, "stored block holds {} bytes, expected {}", payload.size(), expected_size);
      }
      return Bytes(payload.begin(), payload.end());
    }
    case Compression::lzo: return lzo::decompress(payload, expected_size);
    case Compression::zlib: return inflate::zlib(payload, expected_size);
  }
  return fail(ErrorKind::internal, "unreachable compression kind");
}

Result<Bytes> unpack_block(ByteView block, std::size_t expected_size, bool verify_checksum) {
  auto parsed = parse_block(block);
  if (!parsed) return std::unexpected(parsed.error());
  auto data = decompress(parsed->compression, parsed->payload, expected_size);
  if (!data) return std::unexpected(data.error());
  if (verify_checksum) {
    if (const std::uint32_t actual = adler32(*data); actual != parsed->checksum) {
      return fail(ErrorKind::format, "block checksum mismatch: stored {:#010x}, computed {:#010x}", parsed->checksum,
                  actual);
    }
  }
  return data;
}

}  // namespace minedict
