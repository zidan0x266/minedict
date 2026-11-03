// minedict - an LZO1X decompressor.
//
// MDict engine 1.x stores its blocks with LZO1X. The bitstream is documented
// (see the Linux kernel's Documentation/staging/lzo.rst); this decoder
// follows that description and checks every bound, so corrupt input yields
// an error rather than a crash.
#pragma once

#include <cstddef>
#include <span>

#include "minedict/bytes.hpp"

namespace minedict::lzo {

/// Decompress `input` into `output`; returns the number of bytes written.
/// Trailing input after the end-of-stream marker is tolerated.
[[nodiscard]] Result<std::size_t> decompress(ByteView input, std::span<std::byte> output) noexcept;

/// Decompress into a fresh buffer that must come out exactly `expected_size` long.
[[nodiscard]] Result<Bytes> decompress(ByteView input, std::size_t expected_size);

}  // namespace minedict::lzo
