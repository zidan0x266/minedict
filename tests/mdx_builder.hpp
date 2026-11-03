// Test helper: builds small MDX/MDD files in memory so the reader can be
// exercised against every layout variant without shipping binary fixtures.
// zlib blocks are written with our own tiny encoder (stored or fixed-Huffman
// literal blocks), which is enough to drive every branch of the decoder.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "minedict/bytes.hpp"
#include "minedict/checksum.hpp"
#include "minedict/compression.hpp"
#include "minedict/header.hpp"
#include "minedict/ripemd128.hpp"
#include "minedict/unicode.hpp"

namespace minedict::testing {

struct TestEntry {
  std::string key;
  std::string record;  // UTF-8 text for an MDX, raw bytes for an MDD
};

enum class ZlibStyle { stored, fixed_huffman };

struct BuildOptions {
  bool v2{true};
  Compression compression{Compression::zlib};  // zlib or none (no LZO encoder here)
  ZlibStyle zlib_style{ZlibStyle::fixed_huffman};
  bool encrypt_key_index{false};
  bool utf16{false};
  Kind kind{Kind::mdx};
  std::size_t keys_per_block{3};
  std::size_t records_per_block{2};
  std::map<std::string, std::string> attributes;  // extra or overriding header attributes
};

inline void put_be(Bytes& out, std::uint64_t value, std::size_t width) {
  for (std::size_t i = width; i-- > 0;) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
}

inline void put_le(Bytes& out, std::uint64_t value, std::size_t width) {
  for (std::size_t i = 0; i < width; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
}

inline void append(Bytes& out, ByteView bytes) { out.insert(out.end(), bytes.begin(), bytes.end()); }

inline Bytes utf16le(std::string_view utf8) {
  Bytes out;
  const auto put = [&](char32_t unit) {
    out.push_back(static_cast<std::byte>(unit & 0xFF));
    out.push_back(static_cast<std::byte>((unit >> 8) & 0xFF));
  };
  for (const char32_t cp : unicode::to_u32(utf8)) {
    if (cp >= 0x10000) {
      put(0xD800 + ((cp - 0x10000) >> 10));
      put(0xDC00 + ((cp - 0x10000) & 0x3FF));
    } else {
      put(cp);
    }
  }
  return out;
}

inline Bytes encode_text(std::string_view utf8, bool utf16) {
  if (utf16) return utf16le(utf8);
  const ByteView bytes = as_bytes(utf8);
  return Bytes(bytes.begin(), bytes.end());
}

/// LSB-first bit writer for the tiny DEFLATE encoder below.
class BitWriter {
 public:
  void put(std::uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
      if (bit_ == 0) out_.push_back(std::byte{0});
      if ((value >> i) & 1U) out_.back() |= static_cast<std::byte>(1U << bit_);
      bit_ = (bit_ + 1) % 8;
    }
  }
  /// Huffman codes go MSB-first.
  void put_code(std::uint32_t code, unsigned length) {
    for (unsigned i = length; i-- > 0;) put((code >> i) & 1U, 1);
  }
  void align() { bit_ = 0; }
  Bytes& bytes() { return out_; }

 private:
  Bytes out_;
  unsigned bit_{0};
};

/// A zlib stream made of stored blocks (no compression).
inline Bytes zlib_stored(ByteView raw) {
  Bytes out{std::byte{0x78}, std::byte{0x01}};
  std::size_t pos = 0;
  do {
    const std::size_t chunk = std::min<std::size_t>(raw.size() - pos, 65535);
    const bool last = pos + chunk >= raw.size();
    out.push_back(static_cast<std::byte>(last ? 0x01 : 0x00));  // BFINAL, BTYPE=00
    put_le(out, chunk, 2);
    put_le(out, static_cast<std::uint16_t>(~chunk), 2);
    append(out, raw.subspan(pos, chunk));
    pos += chunk;
  } while (pos < raw.size());
  put_be(out, adler32(raw), 4);
  return out;
}

/// A zlib stream with one fixed-Huffman block of literals only (RFC 1951 3.2.6).
inline Bytes zlib_fixed_literals(ByteView raw) {
  BitWriter writer;
  writer.put(1, 1);  // BFINAL
  writer.put(1, 2);  // BTYPE = 01 (fixed Huffman)
  for (const std::byte b : raw) {
    const auto literal = std::to_integer<unsigned>(b);
    if (literal < 144) {
      writer.put_code(0x30 + literal, 8);
    } else {
      writer.put_code(0x190 + (literal - 144), 9);
    }
  }
  writer.put_code(0, 7);  // end of block (symbol 256)
  writer.align();
  Bytes out{std::byte{0x78}, std::byte{0x01}};
  append(out, writer.bytes());
  put_be(out, adler32(raw), 4);
  return out;
}

/// Tag + Adler-32 + payload, as every MDict block is laid out.
inline Bytes make_block(ByteView raw, Compression compression, ZlibStyle style = ZlibStyle::fixed_huffman) {
  Bytes block;
  put_le(block, static_cast<std::uint64_t>(compression), 4);
  put_be(block, adler32(raw), 4);
  if (compression == Compression::zlib) {
    append(block, style == ZlibStyle::stored ? zlib_stored(raw) : zlib_fixed_literals(raw));
  } else {
    append(block, raw);
  }
  return block;
}

/// The inverse of the reader's key-index unscrambling.
inline void scramble_key_index(Bytes& block) {
  std::array<std::byte, 8> seed{};
  std::copy_n(block.begin() + 4, 4, seed.begin());
  seed[4] = std::byte{0x95};
  seed[5] = std::byte{0x36};
  const Digest128 key = ripemd128(seed);
  std::uint8_t previous = 0x36;
  for (std::size_t i = 0; i + 8 < block.size(); ++i) {
    const auto plain = std::to_integer<std::uint8_t>(block[8 + i]);
    const auto mixed = static_cast<std::uint8_t>(plain ^ previous ^ static_cast<std::uint8_t>(i & 0xFF) ^
                                                 std::to_integer<std::uint8_t>(key[i % key.size()]));
    const auto cipher = static_cast<std::uint8_t>(((mixed >> 4) | (mixed << 4)) & 0xFF);
    previous = cipher;
    block[8 + i] = std::byte{cipher};
  }
}

inline std::string xml_escape(std::string_view text) {
  std::string out;
  for (const char c : text) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

inline Bytes build(const std::vector<TestEntry>& entries, BuildOptions options = {}) {
  if (options.kind == Kind::mdd) options.utf16 = true;
  const std::size_t width = options.v2 ? 8 : 4;
  const std::size_t unit = options.utf16 ? 2 : 1;
  const bool mdx = options.kind == Kind::mdx;

  // ---- header ---------------------------------------------------------------
  std::map<std::string, std::string> attributes{
      {"GeneratedByEngineVersion", options.v2 ? "2.0" : "1.2"},
      {"RequiredEngineVersion", options.v2 ? "2.0" : "1.2"},
      {"Encrypted", options.encrypt_key_index ? "2" : "0"},
      {"Encoding", mdx ? (options.utf16 ? "UTF-16" : "UTF-8") : ""},
      {"Format", mdx ? "Html" : ""},
      {"KeyCaseSensitive", "No"},
      {"StripKey", mdx ? "Yes" : "No"},
      {"Title", "Test Dictionary"},
      {"Description", "<b>built</b> for tests"},
      {"CreationDate", "2026-1-1"},
  };
  for (const auto& [name, value] : options.attributes) attributes[name] = value;
  std::string xml = "\xEF\xBB\xBF<";
  xml += mdx ? "Dictionary" : "Library_Data";
  for (const auto& [name, value] : attributes) xml += std::format(" {}=\"{}\"", name, xml_escape(value));
  xml += "/>\r\n";
  const Bytes header = utf16le(xml);

  Bytes out;
  put_be(out, header.size(), 4);
  append(out, header);
  put_le(out, adler32(header), 4);

  // ---- records and their offsets ---------------------------------------------
  std::vector<Bytes> records;
  std::vector<std::uint64_t> offsets;
  std::uint64_t offset = 0;
  for (const TestEntry& entry : entries) {
    Bytes record = mdx ? encode_text(entry.record, options.utf16)
                       : Bytes(as_bytes(entry.record).begin(), as_bytes(entry.record).end());
    if (mdx) record.insert(record.end(), unit, std::byte{0});
    offsets.push_back(offset);
    offset += record.size();
    records.push_back(std::move(record));
  }

  // ---- key blocks and their index ---------------------------------------------
  std::vector<Bytes> key_blocks;
  Bytes key_index;
  for (std::size_t start = 0; start < entries.size(); start += options.keys_per_block) {
    const std::size_t end = std::min(start + options.keys_per_block, entries.size());
    Bytes raw;
    for (std::size_t i = start; i < end; ++i) {
      put_be(raw, offsets[i], width);
      append(raw, encode_text(entries[i].key, options.utf16));
      raw.insert(raw.end(), unit, std::byte{0});
    }
    Bytes block = make_block(raw, options.compression, options.zlib_style);

    put_be(key_index, end - start, width);
    for (const std::string& boundary : {entries[start].key, entries[end - 1].key}) {
      const Bytes text = encode_text(boundary, options.utf16);
      put_be(key_index, text.size() / unit, options.v2 ? 2 : 1);
      append(key_index, text);
      if (options.v2) key_index.insert(key_index.end(), unit, std::byte{0});
    }
    put_be(key_index, block.size(), width);
    put_be(key_index, raw.size(), width);
    key_blocks.push_back(std::move(block));
  }
  Bytes index_bytes = key_index;
  if (options.v2) {
    index_bytes = make_block(key_index, Compression::zlib, options.zlib_style);
    if (options.encrypt_key_index) scramble_key_index(index_bytes);
  }
  std::uint64_t key_blocks_size = 0;
  for (const Bytes& block : key_blocks) key_blocks_size += block.size();

  Bytes key_section;
  put_be(key_section, key_blocks.size(), width);
  put_be(key_section, entries.size(), width);
  if (options.v2) put_be(key_section, key_index.size(), width);
  put_be(key_section, index_bytes.size(), width);
  put_be(key_section, key_blocks_size, width);
  if (options.v2) put_be(key_section, adler32(key_section), 4);
  append(out, key_section);
  append(out, index_bytes);
  for (const Bytes& block : key_blocks) append(out, block);

  // ---- record blocks -----------------------------------------------------------
  std::vector<Bytes> record_blocks;
  std::vector<std::pair<std::uint64_t, std::uint64_t>> sizes;  // compressed, decompressed
  for (std::size_t start = 0; start < records.size(); start += options.records_per_block) {
    const std::size_t end = std::min(start + options.records_per_block, records.size());
    Bytes raw;
    for (std::size_t i = start; i < end; ++i) append(raw, records[i]);
    Bytes block = make_block(raw, options.compression, options.zlib_style);
    sizes.emplace_back(block.size(), raw.size());
    record_blocks.push_back(std::move(block));
  }
  std::uint64_t record_data_size = 0;
  for (const auto& [compressed, decompressed] : sizes) record_data_size += compressed;
  put_be(out, record_blocks.size(), width);
  put_be(out, entries.size(), width);
  put_be(out, record_blocks.size() * 2 * width, width);
  put_be(out, record_data_size, width);
  for (const auto& [compressed, decompressed] : sizes) {
    put_be(out, compressed, width);
    put_be(out, decompressed, width);
  }
  for (const Bytes& block : record_blocks) append(out, block);
  return out;
}

/// A file in the temp directory that disappears with the object.
class TempFile {
 public:
  TempFile(ByteView content, std::string_view extension) {
    static std::mt19937_64 rng{std::random_device{}()};
    path_ = std::filesystem::temp_directory_path() / std::format("minedict-test-{:016x}{}", rng(), extension);
    std::ofstream out(path_, std::ios::binary);
    out.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
  }
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  ~TempFile() {
    std::error_code ec;
    std::filesystem::remove(path_, ec);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

inline std::filesystem::path examples_dir() { return std::filesystem::path(MINEDICT_EXAMPLES_DIR); }

}  // namespace minedict::testing
