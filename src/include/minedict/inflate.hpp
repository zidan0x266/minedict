// minedict - a DEFLATE / zlib decoder (RFC 1950 and RFC 1951).
//
// Engine 2.0 dictionaries compress every block with zlib. This decoder
// handles stored, fixed-Huffman and dynamic-Huffman blocks, checks every
// bound, and verifies the Adler-32 trailer of a zlib stream.
#pragma once

#include <cstddef>
#include <span>

#include "minedict/bytes.hpp"

namespace minedict::inflate {

/// Decode a raw DEFLATE stream into `output`; returns the bytes written.
[[nodiscard]] Result<std::size_t> raw(ByteView input, std::span<std::byte> output) noexcept;

/// Decode a zlib-wrapped stream (2-byte header, DEFLATE data, Adler-32).
[[nodiscard]] Result<std::size_t> zlib(ByteView input, std::span<std::byte> output) noexcept;

/// Decode a zlib stream into a fresh buffer that must be exactly `expected_size` long.
[[nodiscard]] Result<Bytes> zlib(ByteView input, std::size_t expected_size);

}  // namespace minedict::inflate
