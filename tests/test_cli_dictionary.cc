#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "check.hpp"
#include "mdx_builder.hpp"
#include "minedict/cli/dictionary.hpp"
#include "minedict/cli/shelf.hpp"

using namespace minedict;
using namespace minedict::cli;
using minedict::testing::build;
using minedict::testing::examples_dir;
using minedict::testing::TempFile;
using minedict::testing::TestEntry;

namespace {

const std::filesystem::path kExample = examples_dir() / "toy.mdx";

struct Synthetic {
  std::unique_ptr<TempFile> file;
  std::unique_ptr<Dictionary> dictionary;
};

const Synthetic& synthetic() {
  static const Synthetic instance = [] {
    const std::vector<TestEntry> entries{
        {"Apple", "<b>apple</b>"},     {"cherry", "red; see `1`stone`2`"}, {"elder", "@@@LINK=elderberry"},
        {"elderberry", "a berry\r\n"}, {"loop", "@@@LINK=loop"},            {"fig", "one"},
        {"fig", "two"},
    };
    Synthetic s;
    s.file = std::make_unique<TempFile>(
        build(entries, {.attributes = {{"StyleSheet", "1\n<b>\n</b>\n2\n<i>\n</i>"}, {"Title", "<i>Synthetic</i>"}}}),
        ".mdx");
    auto opened = Dictionary::open(s.file->path());
    if (!opened) throw std::runtime_error(opened.error().message);
    s.dictionary = std::make_unique<Dictionary>(std::move(*opened));
    return s;
  }();
  return instance;
}

}  // namespace

TEST(dictionary, metadata_comes_from_the_header) {
  const Metadata& meta = synthetic().dictionary->metadata();
  CHECK_EQ(meta.kind, Kind::mdx);
  CHECK_EQ(meta.title, "Synthetic");  // tags stripped
  CHECK_EQ(meta.entries, 7U);
  CHECK((meta.header.version == Version{2, 0}));
  CHECK_EQ(meta.header.encoding.name, "UTF-8");
  CHECK_EQ(synthetic().dictionary->size(), 7U);
  CHECK_EQ(synthetic().dictionary->key_at(1).key, "cherry");
  CHECK((synthetic().dictionary->key_rules() == KeyRules{.case_sensitive = false, .strip_punctuation = true}));
}

TEST(dictionary, rejects_files_that_are_not_dictionaries) {
  CHECK_EQ(Dictionary::open("does-not-exist.mdx").error().kind, ErrorKind::io);
  const TempFile text(as_bytes("not a dictionary"), ".txt");
  CHECK_EQ(Dictionary::open(text.path()).error().kind, ErrorKind::unsupported);
  const TempFile junk(as_bytes("not a dictionary either, but named like one"), ".mdx");
  CHECK_EQ(Dictionary::open(junk.path()).error().kind, ErrorKind::format);
}

TEST(dictionary, definitions_apply_the_stylesheet_and_are_cleaned) {
  const Dictionary& d = *synthetic().dictionary;
  const auto cherry = d.lookup("cherry");
  REQUIRE(cherry.has_value());
  REQUIRE_EQ(cherry->size(), 1U);
  CHECK_EQ((*cherry)[0].definition, "red; see <b>stone</b><i></i>");
  CHECK_EQ(d.definition_at(3).value_or("?"), "a berry");  // trailing newline removed
}

TEST(dictionary, follows_redirects_and_reports_homographs) {
  const Dictionary& d = *synthetic().dictionary;
  const auto redirected = d.lookup("elder");
  REQUIRE(redirected.has_value());
  REQUIRE_EQ(redirected->size(), 1U);
  CHECK_EQ((*redirected)[0].headword, "elderberry");
  CHECK_EQ((*redirected)[0].via, std::vector<std::string>{"elder"});

  const auto literal = d.lookup("elder", LookupOptions{.follow_links = false});
  REQUIRE(literal.has_value());
  CHECK((*literal)[0].definition.starts_with(link_prefix));

  const auto self = d.lookup("loop");  // a redirect to itself must not recurse forever
  REQUIRE(self.has_value());
  REQUIRE_EQ(self->size(), 1U);
  CHECK_EQ((*self)[0].headword, "loop");

  const auto figs = d.lookup("FIG");
  REQUIRE(figs.has_value());
  REQUIRE_EQ(figs->size(), 2U);
  CHECK_NE((*figs)[0].definition, (*figs)[1].definition);
}

TEST(dictionary, searches_and_suggestions) {
  const Dictionary& d = *synthetic().dictionary;
  const auto prefix = d.find_prefix("eld", 10);
  REQUIRE_EQ(prefix.size(), 2U);
  CHECK_EQ(prefix[0].key, "elder");
  CHECK_EQ(d.find_containing("err", 0).size(), 2U);
  const auto suggestions = d.suggest("chery", 5);
  REQUIRE(!suggestions.empty());
  CHECK_EQ(suggestions[0].key, "cherry");
  CHECK_EQ(suggestions[0].distance, 1U);
  CHECK_EQ(d.resource("x").error().kind, ErrorKind::unsupported);  // not an MDD
}

TEST(dictionary, resources_from_an_mdd) {
  const std::vector<TestEntry> resources{{"\\pic\\cake.png", std::string("\x89PNG\0data", 9)}};
  const TempFile file(build(resources, {.kind = Kind::mdd}), ".mdd");
  const auto opened = Dictionary::open(file.path());
  REQUIRE(opened.has_value());
  CHECK_EQ(opened->metadata().kind, Kind::mdd);
  for (const std::string_view name : {"\\pic\\cake.png", "/pic/cake.png", "pic/cake.png", "\\PIC\\CAKE.PNG"}) {
    const auto bytes = opened->resource(name);
    REQUIRE(bytes.has_value());
    CHECK_EQ(bytes->size(), 9U);
  }
  CHECK_EQ(opened->resource("\\missing.png").error().kind, ErrorKind::not_found);
}

TEST(dictionary, helpers) {
  CHECK_EQ(strip_tags("<b>Oxford</b> &amp; Co"), "Oxford & Co");
  CHECK_EQ(mime_type_for("\\pic\\cake.PNG"), "image/png");
  CHECK_EQ(mime_type_for("sound.mp3"), "audio/mpeg");
  CHECK_EQ(mime_type_for("unknown"), "application/octet-stream");
}

TEST(dictionary, example_dictionary_end_to_end) {
  if (!std::filesystem::exists(kExample)) SKIP("example dictionary not present");
  const auto opened = Dictionary::open(kExample);
  REQUIRE(opened.has_value());
  const Dictionary& d = *opened;
  CHECK_EQ(d.metadata().title, "Toy English-Chinese Dictionary");
  CHECK_EQ(d.metadata().header.encoding.name, "UTF-8");
  CHECK(d.metadata().entries > 2500);

  const auto cake = d.lookup("cake");
  REQUIRE(cake.has_value());
  REQUIRE_EQ(cake->size(), 1U);
  CHECK_EQ((*cake)[0].headword, "cake");
  CHECK((*cake)[0].definition.starts_with("<div class=\"entry\">"));
  CHECK((*cake)[0].definition.contains("蛋糕"));
  CHECK_EQ(d.lookup("CAKE").value()[0].definition, (*cake)[0].definition);

  const auto suggestions = d.suggest("caek", 5);
  REQUIRE(!suggestions.empty());
  CHECK_EQ(suggestions[0].key, "cake");
  CHECK(d.stylesheet() != nullptr);  // examples/theme.css is picked up
}

TEST(shelf, a_word_file_brings_its_resource_files) {
  const TempFile words(build({{"cake", "<img src=\"cake.png\">"}}), ".mdx");
  // Resource files share the word file's name: oxford.mdd, oxford.1.mdd, ...
  std::filesystem::path plain = words.path();
  plain.replace_extension(".mdd");
  std::filesystem::path numbered = words.path();
  numbered.replace_extension(".1.mdd");
  const auto resources = build({{"\\cake.png", "png"}}, {.kind = Kind::mdd});
  for (const auto& file : {plain, numbered}) {
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(resources.data()), static_cast<std::streamsize>(resources.size()));
  }
  struct Cleanup {
    std::vector<std::filesystem::path> files;
    ~Cleanup() {
      for (const auto& file : files) std::filesystem::remove(file);
    }
  } cleanup{{plain, numbered}};

  Shelf shelf;
  REQUIRE(shelf.add(numbered).has_value());      // opened on its own first
  REQUIRE(shelf.add(words.path()).has_value());  // brings the other one, not this one again
  REQUIRE_EQ(shelf.size(), 3U);
  CHECK(shelf.problems().empty());
  CHECK_EQ(shelf.at(0).metadata().kind, Kind::mdd);
  CHECK_EQ(shelf.at(1).metadata().kind, Kind::mdx);
  CHECK_EQ(shelf.at(2).metadata().kind, Kind::mdd);
  CHECK_EQ(shelf.at(2).metadata().path, plain);
  CHECK(shelf.at(2).resource("cake.png").has_value());
  CHECK(shelf.lookup("cake").found());
}

TEST(shelf, aggregates_dictionaries) {
  Shelf shelf;
  REQUIRE(shelf.add(synthetic().file->path()).has_value());
  CHECK_FALSE(shelf.add("missing.mdx").has_value());
  CHECK_EQ(shelf.size(), 1U);

  const auto outcome = shelf.lookup("apple");
  REQUIRE(outcome.found());
  CHECK(outcome.hits[0].dictionary == &shelf.at(0));
  CHECK(outcome.problems.empty());

  const auto completions = shelf.complete("eld", 10);
  REQUIRE_EQ(completions.size(), 2U);
  CHECK_EQ(completions[0], "elder");

  const auto candidates = shelf.suggest("chery", 3);
  REQUIRE(!candidates.empty());
  CHECK_EQ(candidates[0].suggestion.key, "cherry");

  if (std::filesystem::exists(kExample)) {
    Shelf folder;
    REQUIRE(folder.add(examples_dir()).has_value());
    CHECK(folder.lookup("cake").found());
  }
}
