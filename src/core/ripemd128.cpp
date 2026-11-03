#include "minedict/ripemd128.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>

namespace minedict {
namespace {

using Word = std::uint32_t;

constexpr Word f1(Word x, Word y, Word z) noexcept { return x ^ y ^ z; }
constexpr Word f2(Word x, Word y, Word z) noexcept { return (x & y) | (~x & z); }
constexpr Word f3(Word x, Word y, Word z) noexcept { return (x | ~y) ^ z; }
constexpr Word f4(Word x, Word y, Word z) noexcept { return (x & z) | (y & ~z); }

// Message word order and rotation amounts for the 64 steps of each line.
constexpr std::array<std::uint8_t, 64> kLeftOrder{
    0, 1, 2,  3,  4, 5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15, 7, 4, 13, 1, 10, 6,  15, 3,  12, 0, 9,  5, 2,  14, 11, 8,
    3, 10, 14, 4, 9, 15, 8,  1,  2, 7, 0,  6,  13, 11, 5,  12, 1, 9, 11, 10, 0, 8,  12, 4,  13, 3, 7,  15, 14, 5, 6, 2};
constexpr std::array<std::uint8_t, 64> kLeftShift{
    11, 14, 15, 12, 5,  8,  7,  9,  11, 13, 14, 15, 6,  7,  9,  8,  7,  6,  8,  13, 11, 9,  7,  15, 7,  12, 15, 9,  11, 7,  13, 12,
    11, 13, 6,  7,  14, 9,  13, 15, 14, 8,  13, 6,  5,  12, 7,  5,  11, 12, 14, 15, 14, 15, 9,  8,  9,  14, 5,  6,  8,  6,  5,  12};
constexpr std::array<std::uint8_t, 64> kRightOrder{
    5,  14, 7, 0, 9, 2,  11, 4, 13, 6,  15, 8,  1,  10, 3, 12, 6, 11, 3, 7, 0, 13, 5, 10, 14, 15, 8,  12, 4, 9, 1,  2,
    15, 5,  1, 3, 7, 14, 6,  9, 11, 8,  12, 2,  10, 0,  4, 13, 8, 6,  4, 1, 3, 11, 15, 0, 5,  12, 2,  13, 9, 7, 10, 14};
constexpr std::array<std::uint8_t, 64> kRightShift{
    8, 9,  9,  11, 13, 15, 15, 5,  7,  7,  8,  11, 14, 14, 12, 6,  9,  13, 15, 7,  12, 8,  9,  11, 7,  7,  12, 7,  6,  15, 13, 11,
    9, 7,  15, 11, 8,  6,  6,  14, 12, 13, 5,  14, 13, 13, 7,  5,  15, 5,  8,  11, 14, 14, 6,  14, 6,  9,  12, 9,  12, 5,  15, 8};

constexpr std::array<Word, 4> kLeftConstant{0x00000000U, 0x5A827999U, 0x6ED9EBA1U, 0x8F1BBCDCU};
constexpr std::array<Word, 4> kRightConstant{0x50A28BE6U, 0x5C4DD124U, 0x6D703EF3U, 0x00000000U};

constexpr Word mix(unsigned round, bool left, Word x, Word y, Word z) noexcept {
  // The right line runs the boolean functions in reverse order.
  switch (left ? round : 3 - round) {
    case 0: return f1(x, y, z);
    case 1: return f2(x, y, z);
    case 2: return f3(x, y, z);
    default: return f4(x, y, z);
  }
}

void compress(std::array<Word, 4>& state, const std::array<Word, 16>& block) noexcept {
  std::array<Word, 4> left = state;
  std::array<Word, 4> right = state;
  for (unsigned step = 0; step < 64; ++step) {
    const unsigned round = step / 16;
    auto advance = [&](std::array<Word, 4>& v, bool is_left, Word word, unsigned shift) {
      const Word k = is_left ? kLeftConstant[round] : kRightConstant[round];
      const Word t = std::rotl(v[0] + mix(round, is_left, v[1], v[2], v[3]) + word + k, static_cast<int>(shift));
      v[0] = v[3];
      v[3] = v[2];
      v[2] = v[1];
      v[1] = t;
    };
    advance(left, true, block[kLeftOrder[step]], kLeftShift[step]);
    advance(right, false, block[kRightOrder[step]], kRightShift[step]);
  }
  const Word t = state[1] + left[2] + right[3];
  state[1] = state[2] + left[3] + right[0];
  state[2] = state[3] + left[0] + right[1];
  state[3] = state[0] + left[1] + right[2];
  state[0] = t;
}

std::array<Word, 16> load_block(ByteView bytes) noexcept {
  std::array<Word, 16> words{};
  for (std::size_t i = 0; i < 16; ++i) {
    words[i] = static_cast<Word>(read_le(bytes.subspan(i * 4, 4)));
  }
  return words;
}

}  // namespace

Digest128 ripemd128(ByteView message) noexcept {
  std::array<Word, 4> state{0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U};

  std::size_t offset = 0;
  for (; message.size() - offset >= 64; offset += 64) compress(state, load_block(message.subspan(offset, 64)));

  // Padding: 0x80, zeros, then the bit length as a little-endian 64-bit word.
  std::array<std::byte, 128> tail{};
  const std::size_t rest = message.size() - offset;
  std::ranges::copy(message.subspan(offset), tail.begin());
  tail[rest] = std::byte{0x80};
  const std::size_t padded = rest < 56 ? 64 : 128;
  const std::uint64_t bits = static_cast<std::uint64_t>(message.size()) * 8;
  for (std::size_t i = 0; i < 8; ++i) tail[padded - 8 + i] = static_cast<std::byte>((bits >> (8 * i)) & 0xFF);
  for (std::size_t block = 0; block < padded; block += 64) {
    compress(state, load_block(ByteView{tail}.subspan(block, 64)));
  }

  Digest128 digest{};
  for (std::size_t i = 0; i < 4; ++i) {
    for (std::size_t b = 0; b < 4; ++b) digest[i * 4 + b] = static_cast<std::byte>((state[i] >> (8 * b)) & 0xFF);
  }
  return digest;
}

std::string to_hex(ByteView bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (const std::byte b : bytes) {
    const auto v = std::to_integer<unsigned>(b);
    out += digits[v >> 4];
    out += digits[v & 0xF];
  }
  return out;
}

}  // namespace minedict
