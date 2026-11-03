#include <algorithm>
#include <string>
#include <vector>

#include "check.hpp"
#include "mdx_builder.hpp"
#include "minedict/archive.hpp"

using namespace minedict;
using namespace minedict::testing;

namespace {

const std::vector<TestEntry>& entries() {
  static const std::vector<TestEntry> kEntries{
      {"Apple", "<b>apple</b> a fruit"},  {"banana", "yellow"}, {"cherry", "red; see `1`stone`2`"},
      {"date", "sweet"},                  {"elder", "@@@LINK=elderberry"}, {"elderberry", "a berry"},
      {"fig", "purple"},
  };
  return kEntries;
}

void check_round_trip(const Archive& archive) {
  REQUIRE_EQ(archive.size(), entries().size());
  for (std::size_t i = 0; i < entries().size(); ++i) {
    CHECK_EQ(archive.keys()[i].text, entries()[i].key);
    const auto text = archive.text(i);
    REQUIRE(text.has_value());
    CHECK_EQ(*text, entries()[i].record);
  }
}

}  // namespace

TEST(archive, v2_zlib_round_trip) {
  for (const ZlibStyle style : {ZlibStyle::fixed_huffman, ZlibStyle::stored}) {
    const TempFile file(build(entries(), {.zlib_style = style}), ".mdx");
    const auto archive = Archive::open(file.path());
    REQUIRE(archive.has_value());
    check_round_trip(*archive);
    CHECK_EQ(archive->kind(), Kind::mdx);
    CHECK_EQ(archive->header().title, "Test Dictionary");
    CHECK(archive->header().is_v2());
    CHECK_EQ(archive->stats().key_blocks, 3U);
    CHECK_EQ(archive->stats().record_blocks, 4U);
    CHECK_EQ(archive->stats().key_blocks_by_compression[2], 3U);
    CHECK(archive->warnings().empty());
    CHECK_EQ(archive->file_size(), std::filesystem::file_size(file.path()));
  }
}

TEST(archive, v2_scrambled_key_index) {
  const TempFile file(build(entries(), {.encrypt_key_index = true}), ".mdx");
  const auto archive = Archive::open(file.path());
  REQUIRE(archive.has_value());
  CHECK(archive->header().encryption.key_index);
  check_round_trip(*archive);
}

TEST(archive, v1_stored_and_zlib_blocks) {
  for (const Compression compression : {Compression::none, Compression::zlib}) {
    const TempFile file(build(entries(), {.v2 = false, .compression = compression}), ".mdx");
    const auto archive = Archive::open(file.path());
    REQUIRE(archive.has_value());
    CHECK((archive->header().version == Version{1, 2}));
    CHECK_EQ(archive->header().number_width(), NumberWidth::four);
    CHECK_EQ(archive->stats().key_blocks_by_compression[static_cast<std::size_t>(compression)], 3U);
    check_round_trip(*archive);
  }
}

TEST(archive, utf16_text_dictionary) {
  const std::vector<TestEntry> words{{"naïve", "simple"}, {"中文", "Chinese <i>字</i>"}, {"z", "\xF0\x9F\x98\x80 emoji"}};
  const TempFile file(build(words, {.utf16 = true, .keys_per_block = 2}), ".mdx");
  const auto archive = Archive::open(file.path());
  REQUIRE(archive.has_value());
  CHECK_EQ(archive->header().encoding.charset, Charset::utf16le);
  REQUIRE_EQ(archive->size(), 3U);
  for (std::size_t i = 0; i < words.size(); ++i) {
    CHECK_EQ(archive->keys()[i].text, words[i].key);
    CHECK_EQ(archive->text(i).value_or("?"), words[i].record);
  }
}

TEST(archive, mdd_keeps_binary_records_intact) {
  const std::vector<TestEntry> resources{
      {"\\pic\\a.png", std::string("\x89PNG\0\0\x01\0", 8)},
      {"\\snd\\b.bin", std::string("\0\0", 2)},
      {"\\c.txt", "plain"},
  };
  const TempFile file(build(resources, {.kind = Kind::mdd}), ".mdd");
  const auto archive = Archive::open(file.path());
  REQUIRE(archive.has_value());
  CHECK_EQ(archive->kind(), Kind::mdd);
  CHECK_EQ(archive->header().encoding.charset, Charset::utf16le);
  REQUIRE_EQ(archive->size(), 3U);
  for (std::size_t i = 0; i < resources.size(); ++i) {
    CHECK_EQ(archive->keys()[i].text, resources[i].key);
    const auto record = archive->record(i);
    REQUIRE(record.has_value());
    CHECK(std::ranges::equal(*record, as_bytes(resources[i].record)));
  }
}

TEST(archive, single_block_layout_and_out_of_range) {
  const TempFile file(build(entries(), {.keys_per_block = 100, .records_per_block = 100}), ".mdx");
  const auto archive = Archive::open(file.path());
  REQUIRE(archive.has_value());
  CHECK_EQ(archive->stats().key_blocks, 1U);
  CHECK_EQ(archive->stats().record_blocks, 1U);
  check_round_trip(*archive);
  CHECK_EQ(archive->record(entries().size()).error().kind, ErrorKind::invalid_argument);
}

TEST(archive, detects_damaged_record_blocks) {
  Bytes bytes = build(entries(), {.compression = Compression::none});
  bytes[bytes.size() - 3] ^= std::byte{0x55};  // inside the last record block's payload
  const TempFile file(bytes, ".mdx");

  const auto strict = Archive::open(file.path());
  REQUIRE(strict.has_value());  // record blocks are read lazily
  const auto last = strict->text(entries().size() - 1);
  REQUIRE(!last.has_value());
  CHECK(last.error().message.contains("checksum"));
  CHECK(strict->text(0).has_value());  // other blocks are fine

  const auto lenient = Archive::open(file.path(), {.verify_checksums = false});
  REQUIRE(lenient.has_value());
  CHECK(lenient->text(entries().size() - 1).has_value());
}

TEST(archive, rejects_what_is_not_a_dictionary) {
  CHECK_EQ(Archive::open("/definitely/missing.mdx").error().kind, ErrorKind::io);

  const Bytes garbage(200, std::byte{0x42});
  const TempFile junk(garbage, ".mdx");
  CHECK_EQ(Archive::open(junk.path()).error().kind, ErrorKind::format);

  const Bytes whole = build(entries());
  const TempFile truncated(ByteView{whole}.first(whole.size() / 2), ".mdx");
  CHECK_EQ(Archive::open(truncated.path()).error().kind, ErrorKind::format);

  const TempFile locked(build(entries(), {.attributes = {{"Encrypted", "1"}}}), ".mdx");
  CHECK_EQ(Archive::open(locked.path()).error().kind, ErrorKind::unsupported);
}

TEST(archive, read_header_only) {
  const TempFile file(build(entries(), {.attributes = {{"Title", "Peek"}}}), ".mdx");
  const auto header = Archive::read_header(file.path());
  REQUIRE(header.has_value());
  CHECK_EQ(header->title, "Peek");
  CHECK_EQ(header->description, "<b>built</b> for tests");
}

TEST(archive, example_dictionary) {
  const auto path = examples_dir() / "toy.mdx";
  if (!std::filesystem::exists(path)) SKIP("example dictionary not present");

  const auto archive = Archive::open(path);
  REQUIRE(archive.has_value());
  const Header& header = archive->header();
  CHECK((header.version == Version{2, 0}));
  CHECK_EQ(header.encoding.charset, Charset::utf8);
  CHECK_EQ(header.title, "Toy English-Chinese Dictionary");
  CHECK(archive->size() > 2500);
  CHECK(archive->stats().key_blocks_by_compression[2] > 0);  // zlib blocks
  CHECK(archive->warnings().empty());

  const auto keys = archive->keys();
  const auto cake = std::ranges::find(keys, "cake", &KeyEntry::text);
  REQUIRE(cake != keys.end());
  const auto text = archive->text(static_cast<std::size_t>(cake - keys.begin()));
  REQUIRE(text.has_value());
  CHECK(text->contains("cake"));
  CHECK(text->contains("蛋糕"));

  bool all_decode = true;
  for (std::size_t i = 0; i < archive->size(); ++i) all_decode = all_decode && archive->text(i).has_value();
  CHECK(all_decode);
}
