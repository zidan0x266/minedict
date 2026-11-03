#include "minedict/lzo.hpp"

#include <cstdint>

namespace minedict::lzo {
namespace {

constexpr std::size_t kMaxZeroRunBytes = 0x7FFF'FFFF / 255 - 2;  // guards the "zero bytes count 255 each" lengths

enum class Status { ok, input_overrun, output_overrun, lookbehind_overrun, corrupt };

class Decoder {
 public:
  Decoder(ByteView in, std::span<std::byte> out) noexcept : in_(in), out_(out) {}

  Status run(std::size_t& written) noexcept {
    if (in_.size() < 3) return Status::input_overrun;

    std::size_t bitstream_version = 0;
    if (in_.size() >= 5 && byte_at(0) == 17) {  // kernel-style versioned stream
      bitstream_version = byte_at(1);
      ip_ = 2;
    }

    std::size_t literals = 0;  // literals that follow the current instruction
    std::size_t state = 0;     // literals copied by the previous instruction (0..4)

    if (byte_at(ip_) > 17) {
      std::size_t t = byte_at(ip_++) - 17;
      if (t < 4) {
        literals = t;
        if (const Status s = copy_literals(literals); s != Status::ok) return s;
        state = literals;
      } else {
        if (const Status s = copy_literals(t); s != Status::ok) return s;
        state = 4;
      }
    }

    for (;;) {
      if (ip_ >= in_.size()) return Status::input_overrun;
      std::size_t t = byte_at(ip_++);
      std::size_t distance = 0;
      std::size_t length = 0;

      if (t < 16) {
        if (state == 0) {
          // A long literal run: 3 + (L or 15 + zeros * 255 + next byte).
          if (t == 0) {
            const Status s = extend(t, 15);
            if (s != Status::ok) return s;
          }
          t += 3;
          if (const Status s = copy_literals(t); s != Status::ok) return s;
          state = 4;
          continue;
        }
        if (ip_ >= in_.size()) return Status::input_overrun;
        if (state != 4) {
          // Two bytes from at most 1 KiB back.
          literals = t & 3;
          distance = 1 + (t >> 2) + (static_cast<std::size_t>(byte_at(ip_++)) << 2);
          length = 2;
        } else {
          // Three bytes from 2..3 KiB back.
          literals = t & 3;
          distance = 2049 + (t >> 2) + (static_cast<std::size_t>(byte_at(ip_++)) << 2);
          length = 3;
        }
      } else if (t >= 64) {
        // 0 1 L D D D S S / 1 L L D D D S S : short match within 2 KiB.
        if (ip_ >= in_.size()) return Status::input_overrun;
        literals = t & 3;
        distance = 1 + ((t >> 2) & 7) + (static_cast<std::size_t>(byte_at(ip_++)) << 3);
        length = (t >> 5) + 1;  // 3..4 for 0 1 L, 5..8 for 1 L L
      } else if (t >= 32) {
        // 0 0 1 L L L L L : match within 16 KiB, LE16 distance follows.
        length = (t & 31) + 2;
        if (length == 2) {
          const Status s = extend(length, 31);
          if (s != Status::ok) return s;
        }
        if (ip_ + 2 > in_.size()) return Status::input_overrun;
        const std::size_t d = le16(ip_);
        ip_ += 2;
        distance = 1 + (d >> 2);
        literals = d & 3;
      } else {
        // 0 0 0 1 H L L L : match 16..48 KiB back, or end of stream, or
        // (bitstream version 1) a run of zeros.
        if (ip_ + 2 > in_.size()) return Status::input_overrun;
        const std::size_t d = le16(ip_);
        if (bitstream_version != 0 && (d & 0xFFFC) == 0xFFFC && (t & 0xF8) == 0x18) {
          if (ip_ + 3 > in_.size()) return Status::input_overrun;
          const std::size_t zeros = ((t & 7) | (static_cast<std::size_t>(byte_at(ip_ + 2)) << 3)) + 4;
          ip_ += 3;
          if (op_ + zeros > out_.size()) return Status::output_overrun;
          for (std::size_t i = 0; i < zeros; ++i) out_[op_++] = std::byte{0};
          literals = d & 3;
          if (const Status s = copy_literals(literals); s != Status::ok) return s;
          state = literals;
          continue;
        }
        const std::size_t high = (t & 8) << 11;
        length = (t & 7) + 2;
        std::size_t d2 = d;
        if (length == 2) {
          const Status s = extend(length, 7);
          if (s != Status::ok) return s;
          if (ip_ + 2 > in_.size()) return Status::input_overrun;
          d2 = le16(ip_);
        }
        ip_ += 2;
        distance = high + (d2 >> 2);
        literals = d2 & 3;
        if (distance == 0) {  // end of stream marker
          written = op_;
          return length == 3 ? Status::ok : Status::corrupt;
        }
        distance += 16384;
      }

      if (distance > op_) return Status::lookbehind_overrun;
      if (op_ + length > out_.size()) return Status::output_overrun;
      // Byte-wise copy: the source may overlap the destination (run-length style matches).
      for (std::size_t i = 0; i < length; ++i, ++op_) out_[op_] = out_[op_ - distance];

      if (const Status s = copy_literals(literals); s != Status::ok) return s;
      state = literals;
    }
  }

 private:
  [[nodiscard]] std::size_t byte_at(std::size_t i) const noexcept { return std::to_integer<std::size_t>(in_[i]); }
  [[nodiscard]] std::size_t le16(std::size_t i) const noexcept { return byte_at(i) | (byte_at(i + 1) << 8); }

  /// Length extension: each zero byte adds 255, then a non-zero byte is added.
  Status extend(std::size_t& value, std::size_t base) noexcept {
    std::size_t zeros = 0;
    for (;;) {
      if (ip_ >= in_.size()) return Status::input_overrun;
      const std::size_t b = byte_at(ip_++);
      if (b != 0) {
        value += zeros * 255 + base + b;
        return Status::ok;
      }
      if (++zeros > kMaxZeroRunBytes) return Status::corrupt;
    }
  }

  Status copy_literals(std::size_t count) noexcept {
    if (count == 0) return Status::ok;
    if (ip_ + count > in_.size()) return Status::input_overrun;
    if (op_ + count > out_.size()) return Status::output_overrun;
    for (std::size_t i = 0; i < count; ++i) out_[op_++] = in_[ip_++];
    return Status::ok;
  }

  ByteView in_;
  std::span<std::byte> out_;
  std::size_t ip_{0};
  std::size_t op_{0};
};

}  // namespace

Result<std::size_t> decompress(ByteView input, std::span<std::byte> output) noexcept {
  std::size_t written = 0;
  switch (Decoder{input, output}.run(written)) {
    case Status::ok: return written;
    case Status::input_overrun: return fail(ErrorKind::format, "LZO stream ends before its end-of-stream marker");
    case Status::output_overrun: return fail(ErrorKind::format, "LZO stream expands beyond the declared size");
    case Status::lookbehind_overrun: return fail(ErrorKind::format, "LZO match points before the start of the output");
    case Status::corrupt: return fail(ErrorKind::format, "LZO stream is corrupt");
  }
  return fail(ErrorKind::internal, "LZO decoder returned an unknown status");
}

Result<Bytes> decompress(ByteView input, std::size_t expected_size) {
  Bytes out(expected_size);
  auto written = decompress(input, out);
  if (!written) return std::unexpected(written.error());
  if (*written != expected_size) {
    return fail(ErrorKind::format, "LZO block decompressed to {} bytes, expected {}", *written, expected_size);
  }
  return out;
}

}  // namespace minedict::lzo
