#include "minedict/archive.hpp"

#include <algorithm>
#include <future>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "minedict/checksum.hpp"
#include "minedict/encoding.hpp"
#include "minedict/mapped_file.hpp"
#include "minedict/ripemd128.hpp"
#include "minedict/unicode.hpp"

namespace minedict {
namespace {

constexpr std::uint32_t kMaxHeaderBytes = 64U * 1024U * 1024U;

struct RecordBlock {
  std::uint64_t compressed_offset;  // from the start of the record data
  std::uint64_t compressed_size;
  std::uint64_t decompressed_offset;  // in the concatenated decompressed stream
  std::uint64_t decompressed_size;

  [[nodiscard]] std::uint64_t decompressed_end() const noexcept { return decompressed_offset + decompressed_size; }
};

struct KeyBlockInfo {
  std::uint64_t entries;
  std::uint64_t compressed_offset;  // from the start of the key blocks
  std::uint64_t compressed_size;
  std::uint64_t decompressed_size;
};

Kind kind_from_extension(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  for (char& c : extension) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
  }
  return extension == ".mdd" ? Kind::mdd : Kind::mdx;
}

/// Undo the scrambling of an encrypted key-block index (engine 2.0,
/// Encrypted bit 2). The cipher key is RIPEMD-128 of the block's checksum
/// bytes followed by 0x95 0x36 0x00 0x00.
void unscramble_key_index(Bytes& block) {
  if (block.size() < block_prelude_size) return;
  std::array<std::byte, 8> seed{};
  std::copy_n(block.begin() + 4, 4, seed.begin());
  seed[4] = std::byte{0x95};
  seed[5] = std::byte{0x36};
  const Digest128 key = ripemd128(seed);

  std::uint8_t previous = 0x36;
  for (std::size_t i = 0; i + block_prelude_size < block.size(); ++i) {
    const auto b = std::to_integer<std::uint8_t>(block[block_prelude_size + i]);
    std::uint8_t t = static_cast<std::uint8_t>(((b >> 4) | (b << 4)) & 0xFF);
    t ^= previous ^ static_cast<std::uint8_t>(i & 0xFF) ^ std::to_integer<std::uint8_t>(key[i % key.size()]);
    previous = b;
    block[block_prelude_size + i] = std::byte{t};
  }
}

/// Position of the first NUL terminator (1 or 2 bytes wide, aligned) in `data`.
std::size_t find_terminator(ByteView data, std::size_t unit) noexcept {
  if (unit == 1) {
    const auto it = std::ranges::find(data, std::byte{0});
    return static_cast<std::size_t>(it - data.begin());
  }
  for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
    if (data[i] == std::byte{0} && data[i + 1] == std::byte{0}) return i;
  }
  return data.size();
}

/// Keys decoded from a run of key blocks, by one worker.
struct DecodedKeys {
  std::vector<KeyEntry> keys;
  std::array<std::size_t, 3> blocks_by_compression{};
  std::vector<std::string> warnings;
};

Result<DecodedKeys> decode_key_blocks(std::span<const KeyBlockInfo> infos, ByteView blocks_raw, const Header& header,
                                      std::size_t first_block_number, bool verify_checksums) {
  auto decoder = Transcoder::open(header.encoding);
  if (!decoder) return std::unexpected(decoder.error());
  const NumberWidth width = header.number_width();
  const auto number_bytes = static_cast<std::size_t>(width);
  const std::size_t unit = header.encoding.unit_size();

  DecodedKeys out;
  std::size_t expected = 0;
  for (const KeyBlockInfo& info : infos) expected += info.entries;
  out.keys.reserve(expected);

  std::size_t block_number = first_block_number;
  for (const KeyBlockInfo& info : infos) {
    const ByteView block = blocks_raw.subspan(info.compressed_offset, info.compressed_size);
    auto parsed = parse_block(block);
    if (!parsed) return fail(ErrorKind::format, "key block {}: {}", block_number, parsed.error().message);
    out.blocks_by_compression[static_cast<std::size_t>(parsed->compression)]++;
    auto data = unpack_block(block, info.decompressed_size, verify_checksums);
    if (!data) return fail(ErrorKind::format, "key block {}: {}", block_number, data.error().message);

    const ByteView view = *data;
    std::size_t pos = 0;
    while (pos < view.size()) {
      if (pos + number_bytes > view.size()) {
        out.warnings.push_back(std::format("key block {} ends with {} stray bytes", block_number, view.size() - pos));
        break;
      }
      const std::uint64_t offset = read_be(view.subspan(pos, number_bytes));
      pos += number_bytes;
      const ByteView rest = view.subspan(pos);
      const std::size_t end = find_terminator(rest, unit);
      out.keys.push_back(KeyEntry{.text = decoder->to_utf8(rest.first(end)), .record_offset = offset});
      pos += std::min(end + unit, rest.size());
    }
    ++block_number;
  }
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------

struct Archive::Impl {
  MappedFile file;
  Header header;
  OpenOptions options;

  // Key section, parsed at open; the blocks themselves are decoded by load_keys().
  std::vector<KeyBlockInfo> key_infos;
  ByteView key_blocks_raw;
  std::uint64_t declared_entries{0};
  mutable std::mutex keys_mutex;
  mutable bool keys_loaded{false};
  mutable std::vector<KeyEntry> keys;
  mutable std::vector<std::uint64_t> sorted_offsets;  // distinct record offsets, ascending

  std::vector<RecordBlock> blocks;
  std::uint64_t record_data_offset{0};
  mutable ArchiveStats stats;
  mutable std::vector<std::string> warnings;

  struct CachedBlock {
    std::size_t index;
    Bytes data;
  };
  mutable std::mutex record_mutex;
  mutable std::vector<CachedBlock> cache;  // most recently used last
  mutable std::optional<Transcoder> transcoder;

  [[nodiscard]] Result<void> parse();
  [[nodiscard]] Result<void> parse_key_section(Reader& reader);
  [[nodiscard]] Result<void> parse_record_index(Reader& reader);
  [[nodiscard]] Result<void> load_keys() const;
  [[nodiscard]] Result<std::span<const std::byte>> decoded_block(std::size_t index) const;
  [[nodiscard]] Result<Bytes> record(std::size_t index) const;
};

Result<void> Archive::Impl::parse() {
  Reader reader(file.view());
  const std::uint32_t header_size = reader.u32();
  if (!reader.ok() || header_size == 0 || header_size > kMaxHeaderBytes || header_size + 8ULL > file.size()) {
    return fail(ErrorKind::format, "not an MDict file (implausible header length)");
  }
  const ByteView header_bytes = reader.take(header_size);
  const std::uint32_t header_checksum = reader.u32_le();
  if (options.verify_checksums && adler32(header_bytes) != header_checksum) {
    return fail(ErrorKind::format, "header checksum mismatch; the file is damaged or not an MDict file");
  }
  auto parsed = parse_header(header_bytes, kind_from_extension(file.path()));
  if (!parsed) return std::unexpected(parsed.error());
  header = std::move(*parsed);
  if (header.encryption.key_blocks) {
    return fail(ErrorKind::unsupported, "the dictionary is locked to its owner (Encrypted=1); it needs their registration code");
  }
  if (!header.encoding.is_unicode() && header.encoding.charset != Charset::latin1 && !legacy_charsets_supported()) {
    return fail(ErrorKind::unsupported, "the dictionary is encoded as {}, which this build cannot decode (no iconv)",
                header.encoding.name);
  }

  if (const auto status = parse_key_section(reader); !status) return status;
  if (const auto status = parse_record_index(reader); !status) return status;
  if (options.decode_keys) return load_keys();
  return {};
}

Result<void> Archive::Impl::parse_key_section(Reader& reader) {
  const NumberWidth width = header.number_width();
  const bool v2 = header.is_v2();
  const std::size_t section_start = reader.position();

  const std::uint64_t block_count = reader.number(width);
  declared_entries = reader.number(width);
  const std::uint64_t index_decompressed_size = v2 ? reader.number(width) : 0;
  const std::uint64_t index_size = reader.number(width);
  const std::uint64_t blocks_size = reader.number(width);
  if (v2) {
    const ByteView counts = file.view().subspan(section_start, 40);
    const std::uint32_t checksum = reader.u32();
    if (reader.ok() && options.verify_checksums && adler32(counts) != checksum) {
      return fail(ErrorKind::format, "key section checksum mismatch");
    }
  }
  const ByteView index_raw = reader.take(index_size);
  key_blocks_raw = reader.take(blocks_size);
  if (const auto status = reader.status(); !status) {
    return fail(ErrorKind::format, "key section: {}", status.error().message);
  }

  // The index: per block, its entry count, first and last key, and sizes.
  Bytes index_bytes;
  if (v2) {
    Bytes scrambled(index_raw.begin(), index_raw.end());
    if (header.encryption.key_index) unscramble_key_index(scrambled);
    auto unpacked = unpack_block(scrambled, index_decompressed_size, options.verify_checksums);
    if (!unpacked) return fail(ErrorKind::format, "key index: {}", unpacked.error().message);
    index_bytes = std::move(*unpacked);
  } else {
    index_bytes.assign(index_raw.begin(), index_raw.end());
  }

  Reader index(index_bytes);
  const std::size_t unit = header.encoding.unit_size();
  const std::size_t terminator = v2 ? 1 : 0;  // 2.0 counts a terminator after each boundary key
  std::uint64_t counted = 0;
  std::uint64_t compressed = 0;
  key_infos.reserve(block_count);
  for (std::uint64_t i = 0; i < block_count && index.ok(); ++i) {
    KeyBlockInfo info{};
    info.entries = index.number(width);
    const std::size_t first_size = v2 ? index.u16() : index.u8();
    index.skip((first_size + terminator) * unit);
    const std::size_t last_size = v2 ? index.u16() : index.u8();
    index.skip((last_size + terminator) * unit);
    info.compressed_offset = compressed;
    info.compressed_size = index.number(width);
    info.decompressed_size = index.number(width);
    compressed += info.compressed_size;
    counted += info.entries;
    key_infos.push_back(info);
  }
  if (const auto status = index.status(); !status) {
    return fail(ErrorKind::format, "key index: {}", status.error().message);
  }
  if (compressed > key_blocks_raw.size()) {
    return fail(ErrorKind::format, "key index describes {} bytes of blocks but only {} are present", compressed,
                key_blocks_raw.size());
  }
  if (counted != declared_entries) {
    warnings.push_back(std::format("key index counts {} entries but the header says {}", counted, declared_entries));
  }
  if (compressed < key_blocks_raw.size()) {
    warnings.push_back(std::format("{} unused bytes after the key blocks", key_blocks_raw.size() - compressed));
  }
  stats.key_blocks = key_infos.size();
  return {};
}

Result<void> Archive::Impl::load_keys() const {
  std::lock_guard lock(keys_mutex);
  if (keys_loaded) return {};

  // Split the blocks among workers; each decodes its run independently.
  unsigned workers = options.threads != 0 ? options.threads : std::thread::hardware_concurrency();
  workers = std::clamp(workers, 1U, 16U);
  if (key_infos.size() < 8) workers = 1;
  const std::size_t per_worker = (key_infos.size() + workers - 1) / workers;

  std::vector<std::future<Result<DecodedKeys>>> futures;
  for (std::size_t start = 0; start < key_infos.size(); start += per_worker) {
    const std::size_t count = std::min(per_worker, key_infos.size() - start);
    const std::span<const KeyBlockInfo> run{key_infos.data() + start, count};
    if (workers == 1) {
      std::promise<Result<DecodedKeys>> ready;
      ready.set_value(decode_key_blocks(run, key_blocks_raw, header, start, options.verify_checksums));
      futures.push_back(ready.get_future());
    } else {
      futures.push_back(std::async(std::launch::async, decode_key_blocks, run, key_blocks_raw, std::cref(header),
                                   start, options.verify_checksums));
    }
  }

  std::vector<KeyEntry> decoded;
  decoded.reserve(declared_entries);
  std::optional<Error> failure;
  for (auto& future : futures) {
    auto chunk = future.get();
    if (!chunk) {
      if (!failure) failure = chunk.error();
      continue;
    }
    decoded.insert(decoded.end(), std::make_move_iterator(chunk->keys.begin()), std::make_move_iterator(chunk->keys.end()));
    for (std::size_t i = 0; i < chunk->blocks_by_compression.size(); ++i) {
      stats.key_blocks_by_compression[i] += chunk->blocks_by_compression[i];
    }
    warnings.insert(warnings.end(), chunk->warnings.begin(), chunk->warnings.end());
  }
  if (failure) return std::unexpected(*failure);
  if (decoded.size() != declared_entries) {
    warnings.push_back(std::format("decoded {} keys but the header announces {}", decoded.size(), declared_entries));
  }
  if (decoded.empty()) return fail(ErrorKind::format, "the dictionary has no keys");

  sorted_offsets.reserve(decoded.size());
  for (const KeyEntry& key : decoded) sorted_offsets.push_back(key.record_offset);
  std::ranges::sort(sorted_offsets);
  const auto [first, last] = std::ranges::unique(sorted_offsets);
  sorted_offsets.erase(first, last);
  keys = std::move(decoded);
  keys_loaded = true;
  return {};
}

Result<void> Archive::Impl::parse_record_index(Reader& reader) {
  const NumberWidth width = header.number_width();
  const std::uint64_t block_count = reader.number(width);
  const std::uint64_t entry_count = reader.number(width);
  const std::uint64_t index_size = reader.number(width);
  const std::uint64_t data_size = reader.number(width);
  const ByteView index_raw = reader.take(index_size);
  if (const auto status = reader.status(); !status) {
    return fail(ErrorKind::format, "record section: {}", status.error().message);
  }
  if (entry_count != declared_entries) {
    warnings.push_back(std::format("record section counts {} entries, key section {}", entry_count, declared_entries));
  }

  Reader index(index_raw);
  blocks.reserve(block_count);
  std::uint64_t compressed = 0;
  std::uint64_t decompressed = 0;
  for (std::uint64_t i = 0; i < block_count && index.ok(); ++i) {
    const std::uint64_t compressed_size = index.number(width);
    const std::uint64_t decompressed_size = index.number(width);
    blocks.push_back(RecordBlock{.compressed_offset = compressed,
                                 .compressed_size = compressed_size,
                                 .decompressed_offset = decompressed,
                                 .decompressed_size = decompressed_size});
    compressed += compressed_size;
    decompressed += decompressed_size;
  }
  if (const auto status = index.status(); !status) {
    return fail(ErrorKind::format, "record index: {}", status.error().message);
  }
  if (compressed != data_size) {
    warnings.push_back(std::format("record index sums to {} bytes but the header says {}", compressed, data_size));
  }
  record_data_offset = reader.position();
  if (compressed > reader.remaining()) {
    return fail(ErrorKind::format, "record data is truncated: {} bytes promised, {} present", compressed,
                reader.remaining());
  }
  if (reader.remaining() > compressed) {
    warnings.push_back(std::format("{} unused bytes after the record data", reader.remaining() - compressed));
  }
  stats.record_blocks = blocks.size();
  stats.record_bytes = decompressed;
  stats.compressed_record_bytes = compressed;
  return {};
}

Result<std::span<const std::byte>> Archive::Impl::decoded_block(std::size_t index) const {
  // Caller holds `record_mutex`.
  if (const auto hit = std::ranges::find(cache, index, &CachedBlock::index); hit != cache.end()) {
    std::rotate(hit, hit + 1, cache.end());  // move to the back = most recent
    return ByteView{cache.back().data};
  }
  const RecordBlock& block = blocks[index];
  const ByteView raw = file.view().subspan(record_data_offset + block.compressed_offset, block.compressed_size);
  auto data = unpack_block(raw, block.decompressed_size, options.verify_checksums);
  if (!data) return fail(ErrorKind::format, "record block {}: {}", index, data.error().message);
  if (!cache.empty() && cache.size() >= std::max<std::size_t>(options.record_cache_blocks, 1)) cache.erase(cache.begin());
  cache.push_back(CachedBlock{.index = index, .data = std::move(*data)});
  return ByteView{cache.back().data};
}

Result<Bytes> Archive::Impl::record(std::size_t index) const {
  if (const auto status = load_keys(); !status) return std::unexpected(status.error());
  if (index >= keys.size()) return fail(ErrorKind::invalid_argument, "key position {} is out of range", index);
  const KeyEntry& entry = keys[index];

  // The block whose decompressed range holds the record's start.
  auto block_it = std::ranges::upper_bound(blocks, entry.record_offset, {}, &RecordBlock::decompressed_offset);
  if (block_it == blocks.begin()) return fail(ErrorKind::format, "record of '{}' lies before the first block", entry.text);
  --block_it;
  const RecordBlock& block = *block_it;
  if (entry.record_offset >= block.decompressed_end()) {
    return fail(ErrorKind::format, "record of '{}' points past the record data", entry.text);
  }

  // It ends where the next record begins, or at the end of the block.
  std::uint64_t end = block.decompressed_end();
  if (const auto next = std::ranges::upper_bound(sorted_offsets, entry.record_offset);
      next != sorted_offsets.end() && *next < end) {
    end = *next;
  }

  std::lock_guard lock(record_mutex);
  auto data = decoded_block(static_cast<std::size_t>(block_it - blocks.begin()));
  if (!data) return std::unexpected(data.error());
  const std::size_t start = static_cast<std::size_t>(entry.record_offset - block.decompressed_offset);
  const std::size_t stop = std::min(static_cast<std::size_t>(end - block.decompressed_offset), data->size());
  if (start > stop) return fail(ErrorKind::format, "record of '{}' has a negative length", entry.text);
  return Bytes(data->begin() + static_cast<std::ptrdiff_t>(start), data->begin() + static_cast<std::ptrdiff_t>(stop));
}

// ---------------------------------------------------------------------------

Archive::Archive(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Archive::Archive(Archive&&) noexcept = default;
Archive& Archive::operator=(Archive&&) noexcept = default;
Archive::~Archive() = default;

Result<Archive> Archive::open(const std::filesystem::path& path, OpenOptions options) {
  auto file = MappedFile::open(path);
  if (!file) return std::unexpected(file.error());
  auto impl = std::make_unique<Impl>();
  impl->file = std::move(*file);
  impl->options = options;
  if (const auto status = impl->parse(); !status) {
    return fail(status.error().kind, "{}: {}", path.string(), status.error().message);
  }
  return Archive{std::move(impl)};
}

Result<Header> Archive::read_header(const std::filesystem::path& path) {
  auto file = MappedFile::open(path);
  if (!file) return std::unexpected(file.error());
  Reader reader(file->view());
  const std::uint32_t header_size = reader.u32();
  if (!reader.ok() || header_size == 0 || header_size > kMaxHeaderBytes || header_size + 8ULL > file->size()) {
    return fail(ErrorKind::format, "{}: not an MDict file (implausible header length)", path.string());
  }
  const ByteView header_bytes = reader.take(header_size);
  const std::uint32_t checksum = reader.u32_le();
  if (adler32(header_bytes) != checksum) {
    return fail(ErrorKind::format, "{}: header checksum mismatch; not an MDict file", path.string());
  }
  auto header = parse_header(header_bytes, kind_from_extension(path));
  if (!header) return fail(header.error().kind, "{}: {}", path.string(), header.error().message);
  return header;
}

const Header& Archive::header() const noexcept { return impl_->header; }
Kind Archive::kind() const noexcept { return impl_->header.kind; }
const std::filesystem::path& Archive::path() const noexcept { return impl_->file.path(); }
std::uint64_t Archive::file_size() const noexcept { return impl_->file.size(); }

Result<void> Archive::load_keys() const {
  if (const auto status = impl_->load_keys(); !status) {
    return fail(status.error().kind, "{}: {}", impl_->file.path().string(), status.error().message);
  }
  return {};
}

bool Archive::keys_loaded() const noexcept {
  std::lock_guard lock(impl_->keys_mutex);
  return impl_->keys_loaded;
}

std::span<const KeyEntry> Archive::keys() const noexcept { return impl_->keys; }
std::size_t Archive::size() const noexcept { return impl_->keys.size(); }
std::uint64_t Archive::declared_entries() const noexcept { return impl_->declared_entries; }
const ArchiveStats& Archive::stats() const noexcept { return impl_->stats; }
std::span<const std::string> Archive::warnings() const noexcept { return impl_->warnings; }

Result<Bytes> Archive::record(std::size_t index) const { return impl_->record(index); }

Result<std::string> Archive::text(std::size_t index) const {
  auto bytes = record(index);
  if (!bytes) return std::unexpected(bytes.error());
  // Drop the terminator(s), one code unit at a time so a UTF-16 'e' (65 00)
  // keeps its high byte; some builders emit more than one terminator.
  const std::size_t unit = impl_->header.encoding.unit_size();
  const auto is_null_unit = [&] {
    if (bytes->size() < unit) return false;
    return std::all_of(bytes->end() - static_cast<std::ptrdiff_t>(unit), bytes->end(),
                       [](std::byte b) { return b == std::byte{0}; });
  };
  while (is_null_unit()) bytes->resize(bytes->size() - unit);
  std::lock_guard lock(impl_->record_mutex);
  if (!impl_->transcoder) {
    auto transcoder = Transcoder::open(impl_->header.encoding);
    if (!transcoder) return std::unexpected(transcoder.error());
    impl_->transcoder.emplace(std::move(*transcoder));
  }
  return impl_->transcoder->to_utf8(*bytes);
}

}  // namespace minedict
