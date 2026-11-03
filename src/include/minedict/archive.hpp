// minedict - one MDX or MDD file: its header, every key, and lazy access
// to the records the keys point at.
//
// File layout (engine 1.x uses 4-byte integers, 2.0 uses 8-byte ones):
//
//   header        u32 length | UTF-16LE XML tag | u32 Adler-32 (little-endian)
//   key section   counts and sizes | key-block index | key blocks
//   record section counts and sizes | record-block index | record blocks
//
// Every block is a compression tag, an Adler-32 and the payload (see
// compression.hpp). Keys are decoded eagerly because every query needs
// them; record blocks are decoded on demand and cached.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "minedict/bytes.hpp"
#include "minedict/compression.hpp"
#include "minedict/header.hpp"

namespace minedict {

struct KeyEntry {
  std::string text;             // UTF-8, as stored (not normalised)
  std::uint64_t record_offset;  // into the concatenated decompressed record data
};

struct OpenOptions {
  bool verify_checksums{true};
  std::size_t record_cache_blocks{8};
  /// Decode the key blocks while opening. When false they are decoded on the
  /// first call to `load_keys()`, which resource files rarely need.
  bool decode_keys{true};
  /// Worker threads for decoding key blocks (0 = one per hardware thread).
  unsigned threads{0};
};

struct ArchiveStats {
  std::size_t key_blocks{0};
  std::size_t record_blocks{0};
  std::uint64_t record_bytes{0};             // decompressed
  std::uint64_t compressed_record_bytes{0};  // as stored
  std::array<std::size_t, 3> key_blocks_by_compression{};  // indexed by Compression
};

class Archive {
 public:
  [[nodiscard]] static Result<Archive> open(const std::filesystem::path& path, OpenOptions options = {});

  /// Only the header, without touching the key or record sections.
  [[nodiscard]] static Result<Header> read_header(const std::filesystem::path& path);

  Archive(Archive&&) noexcept;
  Archive& operator=(Archive&&) noexcept;
  Archive(const Archive&) = delete;
  Archive& operator=(const Archive&) = delete;
  ~Archive();

  [[nodiscard]] const Header& header() const noexcept;
  [[nodiscard]] Kind kind() const noexcept;
  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] std::uint64_t file_size() const noexcept;

  /// Decode the key blocks if that has not happened yet (thread-safe, idempotent).
  [[nodiscard]] Result<void> load_keys() const;
  [[nodiscard]] bool keys_loaded() const noexcept;

  /// Every key in file order (the dictionary's own sort order); empty until
  /// the keys are loaded.
  [[nodiscard]] std::span<const KeyEntry> keys() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  /// The entry count the file announces, available before the keys are decoded.
  [[nodiscard]] std::uint64_t declared_entries() const noexcept;

  /// The raw bytes of the record for key `index`: binary data for an MDD,
  /// text in the file's encoding (terminator included) for an MDX. Loads the
  /// keys first when needed.
  [[nodiscard]] Result<Bytes> record(std::size_t index) const;

  /// The record for key `index` as UTF-8 text with its terminator removed.
  [[nodiscard]] Result<std::string> text(std::size_t index) const;

  [[nodiscard]] const ArchiveStats& stats() const noexcept;

  /// Oddities noticed while opening that were not fatal (entry counts that
  /// disagree, trailing bytes, ...).
  [[nodiscard]] std::span<const std::string> warnings() const noexcept;

 private:
  struct Impl;
  explicit Archive(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

}  // namespace minedict
