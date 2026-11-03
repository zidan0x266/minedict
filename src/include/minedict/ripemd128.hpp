// minedict - RIPEMD-128, the hash MDict derives its key-index cipher key from.
#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "minedict/bytes.hpp"

namespace minedict {

using Digest128 = std::array<std::byte, 16>;

[[nodiscard]] Digest128 ripemd128(ByteView message) noexcept;

/// Lower-case hexadecimal rendering of a byte sequence.
[[nodiscard]] std::string to_hex(ByteView bytes);

}  // namespace minedict
