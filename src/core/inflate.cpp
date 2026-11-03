#include "minedict/inflate.hpp"

#include <array>
#include <cstdint>

#include "minedict/checksum.hpp"

namespace minedict::inflate {
namespace {

enum class Status { ok, input_overrun, output_overrun, corrupt, unsupported };

// Length and distance symbol tables (RFC 1951, section 3.2.5).
constexpr std::array<std::uint16_t, 29> kLengthBase{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<std::uint8_t, 29> kLengthExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<std::uint16_t, 30> kDistanceBase{1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                                      33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                                      1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, 30> kDistanceExtra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
// Order in which code-length code lengths are transmitted.
constexpr std::array<std::uint8_t, 19> kCodeLengthOrder{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

constexpr unsigned kMaxBits = 15;
constexpr unsigned kFastBits = 9;

/// LSB-first bit reader. Reading past the end yields zero bits and raises
/// `overrun`, so callers can check once at the end of a block.
class BitReader {
 public:
  explicit BitReader(ByteView input) noexcept : input_(input) {}

  [[nodiscard]] std::uint32_t peek(unsigned n) noexcept {
    refill();
    return buffer_ & ((1U << n) - 1U);
  }

  void drop(unsigned n) noexcept {
    if (n > count_) {
      overrun_ = true;
      buffer_ = 0;
      count_ = 0;
      return;
    }
    buffer_ >>= n;
    count_ -= n;
  }

  [[nodiscard]] std::uint32_t bits(unsigned n) noexcept {
    if (n == 0) return 0;
    const std::uint32_t value = peek(n);
    drop(n);
    return value;
  }

  void align_to_byte() noexcept { drop(count_ % 8); }

  /// After `align_to_byte`: copy `n` raw bytes, draining the bit buffer first.
  [[nodiscard]] bool copy_bytes(std::span<std::byte> out) noexcept {
    for (std::byte& b : out) {
      if (count_ >= 8) {
        b = static_cast<std::byte>(buffer_ & 0xFF);
        buffer_ >>= 8;
        count_ -= 8;
      } else if (pos_ < input_.size()) {
        b = input_[pos_++];
      } else {
        overrun_ = true;
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool overrun() const noexcept { return overrun_; }

  /// Bytes of input not yet consumed (whole bytes still in the bit buffer count).
  [[nodiscard]] std::size_t unread_bytes() const noexcept { return input_.size() - pos_ + count_ / 8; }

 private:
  void refill() noexcept {
    while (count_ <= 24 && pos_ < input_.size()) {
      buffer_ |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input_[pos_++])) << count_;
      count_ += 8;
    }
  }

  ByteView input_;
  std::size_t pos_{0};
  std::uint32_t buffer_{0};
  unsigned count_{0};
  bool overrun_{false};
};

/// A canonical Huffman code with a fast lookup table for short codes.
class Huffman {
 public:
  [[nodiscard]] Status build(std::span<const std::uint8_t> lengths) noexcept {
    count_.fill(0);
    fast_.fill(0);
    for (const std::uint8_t length : lengths) count_[length]++;
    count_[0] = 0;

    // Reject over-subscribed codes; incomplete ones are allowed.
    int left = 1;
    for (unsigned len = 1; len <= kMaxBits; ++len) {
      left <<= 1;
      left -= count_[len];
      if (left < 0) return Status::corrupt;
    }

    std::array<std::uint16_t, kMaxBits + 2> offsets{};
    for (unsigned len = 1; len <= kMaxBits; ++len) offsets[len + 1] = static_cast<std::uint16_t>(offsets[len] + count_[len]);

    std::array<std::uint16_t, kMaxBits + 1> next_code{};
    std::uint16_t code = 0;
    for (unsigned len = 1; len <= kMaxBits; ++len) {
      code = static_cast<std::uint16_t>((code + count_[len - 1]) << 1);
      next_code[len] = code;
    }
    for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
      const unsigned len = lengths[symbol];
      if (len == 0) continue;
      symbols_[offsets[len]++] = static_cast<std::uint16_t>(symbol);
      const std::uint16_t canonical = next_code[len]++;
      if (len <= kFastBits) {
        // The stream delivers codes MSB-first, which lands bit-reversed in an LSB-first peek.
        std::uint32_t reversed = 0;
        for (unsigned i = 0; i < len; ++i) reversed |= ((canonical >> i) & 1U) << (len - 1 - i);
        for (std::uint32_t fill = reversed; fill < fast_.size(); fill += 1U << len) {
          fast_[fill] = static_cast<std::uint16_t>((len << kFastBits) | symbol);
        }
      }
    }
    return Status::ok;
  }

  /// Decode one symbol, or -1 for an invalid code.
  [[nodiscard]] int decode(BitReader& reader) const noexcept {
    const std::uint32_t window = reader.peek(kFastBits);
    if (const std::uint16_t entry = fast_[window]; entry != 0) {
      reader.drop(entry >> kFastBits);
      return entry & ((1U << kFastBits) - 1U);
    }
    // Longer codes: walk the canonical code bit by bit.
    int code = 0;
    int first = 0;
    int index = 0;
    for (unsigned len = 1; len <= kMaxBits; ++len) {
      code |= static_cast<int>(reader.bits(1));
      const int count = count_[len];
      if (code - count < first) return symbols_[static_cast<std::size_t>(index + (code - first))];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
    }
    return -1;
  }

 private:
  std::array<std::uint16_t, kMaxBits + 1> count_{};
  std::array<std::uint16_t, 320> symbols_{};
  std::array<std::uint16_t, 1U << kFastBits> fast_{};
};

class Inflater {
 public:
  Inflater(ByteView input, std::span<std::byte> output) noexcept : reader_(input), out_(output) {}

  Status run(std::size_t& written) noexcept {
    bool last = false;
    while (!last) {
      last = reader_.bits(1) != 0;
      const std::uint32_t type = reader_.bits(2);
      Status status = Status::ok;
      switch (type) {
        case 0: status = stored(); break;
        case 1: status = fixed(); break;
        case 2: status = dynamic(); break;
        default: return Status::corrupt;
      }
      if (status != Status::ok) return status;
      if (reader_.overrun()) return Status::input_overrun;
    }
    written = op_;
    return Status::ok;
  }

  [[nodiscard]] std::size_t unread_bytes() const noexcept { return reader_.unread_bytes(); }

 private:
  Status stored() noexcept {
    reader_.align_to_byte();
    const std::uint32_t length = reader_.bits(16);
    const std::uint32_t complement = reader_.bits(16);
    if (reader_.overrun()) return Status::input_overrun;
    if ((length ^ 0xFFFFU) != complement) return Status::corrupt;
    if (op_ + length > out_.size()) return Status::output_overrun;
    if (!reader_.copy_bytes(out_.subspan(op_, length))) return Status::input_overrun;
    op_ += length;
    return Status::ok;
  }

  Status fixed() noexcept {
    std::array<std::uint8_t, 288> literal_lengths{};
    for (std::size_t i = 0; i < 144; ++i) literal_lengths[i] = 8;
    for (std::size_t i = 144; i < 256; ++i) literal_lengths[i] = 9;
    for (std::size_t i = 256; i < 280; ++i) literal_lengths[i] = 7;
    for (std::size_t i = 280; i < 288; ++i) literal_lengths[i] = 8;
    std::array<std::uint8_t, 30> distance_lengths{};
    distance_lengths.fill(5);
    if (literals_.build(literal_lengths) != Status::ok) return Status::corrupt;
    if (distances_.build(distance_lengths) != Status::ok) return Status::corrupt;
    return codes();
  }

  Status dynamic() noexcept {
    const std::uint32_t literal_count = reader_.bits(5) + 257;
    const std::uint32_t distance_count = reader_.bits(5) + 1;
    const std::uint32_t code_length_count = reader_.bits(4) + 4;
    if (literal_count > 286 || distance_count > 30) return Status::corrupt;

    std::array<std::uint8_t, 19> code_lengths{};
    for (std::uint32_t i = 0; i < code_length_count; ++i) {
      code_lengths[kCodeLengthOrder[i]] = static_cast<std::uint8_t>(reader_.bits(3));
    }
    Huffman code_length_code;
    if (code_length_code.build(code_lengths) != Status::ok) return Status::corrupt;

    std::array<std::uint8_t, 320> lengths{};
    for (std::uint32_t index = 0; index < literal_count + distance_count;) {
      const int symbol = code_length_code.decode(reader_);
      if (symbol < 0) return Status::corrupt;
      if (symbol < 16) {
        lengths[index++] = static_cast<std::uint8_t>(symbol);
        continue;
      }
      std::uint8_t value = 0;
      std::uint32_t repeat = 0;
      if (symbol == 16) {
        if (index == 0) return Status::corrupt;
        value = lengths[index - 1];
        repeat = 3 + reader_.bits(2);
      } else if (symbol == 17) {
        repeat = 3 + reader_.bits(3);
      } else {
        repeat = 11 + reader_.bits(7);
      }
      if (index + repeat > literal_count + distance_count) return Status::corrupt;
      while (repeat-- > 0) lengths[index++] = value;
    }
    if (reader_.overrun()) return Status::input_overrun;
    if (lengths[256] == 0) return Status::corrupt;  // no end-of-block code

    if (literals_.build(std::span(lengths).first(literal_count)) != Status::ok) return Status::corrupt;
    if (distances_.build(std::span(lengths).subspan(literal_count, distance_count)) != Status::ok) {
      return Status::corrupt;
    }
    return codes();
  }

  /// Decode literal/length and distance codes until the end-of-block symbol.
  Status codes() noexcept {
    for (;;) {
      const int symbol = literals_.decode(reader_);
      if (symbol < 0) return Status::corrupt;
      if (reader_.overrun()) return Status::input_overrun;
      if (symbol < 256) {
        if (op_ >= out_.size()) return Status::output_overrun;
        out_[op_++] = static_cast<std::byte>(symbol);
        continue;
      }
      if (symbol == 256) return Status::ok;
      const std::size_t length_index = static_cast<std::size_t>(symbol - 257);
      if (length_index >= kLengthBase.size()) return Status::corrupt;
      const std::size_t length = kLengthBase[length_index] + reader_.bits(kLengthExtra[length_index]);

      const int distance_symbol = distances_.decode(reader_);
      if (distance_symbol < 0 || static_cast<std::size_t>(distance_symbol) >= kDistanceBase.size()) {
        return Status::corrupt;
      }
      const std::size_t distance = kDistanceBase[static_cast<std::size_t>(distance_symbol)] +
                                   reader_.bits(kDistanceExtra[static_cast<std::size_t>(distance_symbol)]);
      if (reader_.overrun()) return Status::input_overrun;
      if (distance > op_) return Status::corrupt;
      if (op_ + length > out_.size()) return Status::output_overrun;
      for (std::size_t i = 0; i < length; ++i, ++op_) out_[op_] = out_[op_ - distance];
    }
  }

  BitReader reader_;
  std::span<std::byte> out_;
  std::size_t op_{0};
  Huffman literals_;
  Huffman distances_;
};

std::unexpected<Error> describe(Status status) {
  switch (status) {
    case Status::input_overrun: return fail(ErrorKind::format, "deflate stream ends before its final block");
    case Status::output_overrun: return fail(ErrorKind::format, "deflate stream expands beyond the declared size");
    case Status::unsupported: return fail(ErrorKind::unsupported, "zlib stream needs a preset dictionary");
    case Status::corrupt:
    case Status::ok: break;
  }
  return fail(ErrorKind::format, "deflate stream is corrupt");
}

}  // namespace

Result<std::size_t> raw(ByteView input, std::span<std::byte> output) noexcept {
  std::size_t written = 0;
  Inflater inflater{input, output};
  if (const Status status = inflater.run(written); status != Status::ok) return describe(status);
  return written;
}

Result<std::size_t> zlib(ByteView input, std::span<std::byte> output) noexcept {
  if (input.size() < 6) return fail(ErrorKind::format, "zlib stream is too short");
  const auto cmf = std::to_integer<unsigned>(input[0]);
  const auto flg = std::to_integer<unsigned>(input[1]);
  if ((cmf & 0x0F) != 8 || (cmf >> 4) > 7) return fail(ErrorKind::format, "not a zlib stream (bad method)");
  if ((cmf * 256 + flg) % 31 != 0) return fail(ErrorKind::format, "not a zlib stream (bad header check)");
  if ((flg & 0x20) != 0) return describe(Status::unsupported);

  std::size_t written = 0;
  Inflater inflater{input.subspan(2), output};
  if (const Status status = inflater.run(written); status != Status::ok) return describe(status);

  const std::size_t trailer_at = input.size() - inflater.unread_bytes();
  if (trailer_at + 4 > input.size()) return fail(ErrorKind::format, "zlib stream is missing its Adler-32 trailer");
  const auto expected = static_cast<std::uint32_t>(read_be(input.subspan(trailer_at, 4)));
  if (adler32(output.first(written)) != expected) return fail(ErrorKind::format, "zlib Adler-32 trailer mismatch");
  return written;
}

Result<Bytes> zlib(ByteView input, std::size_t expected_size) {
  Bytes out(expected_size);
  auto written = zlib(input, out);
  if (!written) return std::unexpected(written.error());
  if (*written != expected_size) {
    return fail(ErrorKind::format, "zlib block inflated to {} bytes, expected {}", *written, expected_size);
  }
  return out;
}

}  // namespace minedict::inflate
