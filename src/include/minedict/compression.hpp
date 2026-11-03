// minedict - the compressed-block envelope shared by key and record blocks.
//
// Every block starts with a 4-byte little-endian compression tag and the
// big-endian Adler-32 of the decompressed data, followed by the payload.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "minedict/bytes.hpp"

namespace minedict {

enum class Compression : std::uint8_t { none = 0, lzo = 1, zlib = 2 };

[[nodiscard]] constexpr std::string_view to_string(Compression compression) noexcept {
  switch (compression) {
    case Compression::none: return "stored";
    case Compression::lzo: return "lzo";
    case Compression::zlib: return "zlib";
  }
  return "unknown";
}

struct Block {
  Compression compression;
  std::uint32_t checksum;  // Adler-32 of the decompressed bytes
  ByteView payload;
};

inline constexpr std::size_t block_prelude_size = 8;

/// Split a block into its tag, checksum and payload.
[[nodiscard]] Result<Block> parse_block(ByteView block);

/// Inflate `payload` according to `compression`; the result is exactly
/// `expected_size` bytes or an error.
[[nodiscard]] Result<Bytes> decompress(Compression compression, ByteView payload, std::size_t expected_size);

/// `parse_block` + `decompress` + checksum verification in one step.
[[nodiscard]] Result<Bytes> unpack_block(ByteView block, std::size_t expected_size, bool verify_checksum = true);

}  // namespace minedict
