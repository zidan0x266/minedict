// minedict - Adler-32, the checksum MDict uses for every block.
#pragma once

#include <cstdint>

#include "minedict/bytes.hpp"

namespace minedict {

[[nodiscard]] constexpr std::uint32_t adler32(ByteView data, std::uint32_t seed = 1) noexcept {
  constexpr std::uint32_t modulus = 65521;
  constexpr std::size_t largest_safe_run = 5552;  // sums stay below 2^32 for this many bytes
  std::uint32_t a = seed & 0xFFFFU;
  std::uint32_t b = seed >> 16U;
  while (!data.empty()) {
    const std::size_t run = data.size() < largest_safe_run ? data.size() : largest_safe_run;
    for (const std::byte byte : data.first(run)) {
      a += std::to_integer<std::uint32_t>(byte);
      b += a;
    }
    a %= modulus;
    b %= modulus;
    data = data.subspan(run);
  }
  return (b << 16U) | a;
}

}  // namespace minedict
