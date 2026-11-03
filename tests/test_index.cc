#include <memory>
#include <vector>

#include "check.hpp"
#include "mdx_builder.hpp"
#include "minedict/archive.hpp"
#include "minedict/index.hpp"

using namespace minedict;
using namespace minedict::testing;

namespace {

struct Fixture {
  std::unique_ptr<TempFile> file;
  std::unique_ptr<Archive> archive;
};

const Fixture& fixture() {
  static const Fixture instance = [] {
    const std::vector<TestEntry> entries{
        {"Apple", "fruit"},  {"banana", "yellow"},  {"cherry", "red"}, {"date", "sweet"},
        {"elder", "tree"},   {"elderberry", "berry"}, {"fig", "purple"}, {"fig", "second sense"},
        {"ice-cream", "cold"},
    };
    Fixture f;
    f.file = std::make_unique<TempFile>(build(entries), ".mdx");
    auto opened = Archive::open(f.file->path());
    if (!opened) throw std::runtime_error(opened.error().message);
    f.archive = std::make_unique<Archive>(std::move(*opened));
    return f;
  }();
  return instance;
}

Index make_index(KeyRules rules = {}) { return Index{fixture().archive->keys(), rules}; }
using Positions = std::vector<std::uint32_t>;

}  // namespace

TEST(index, exact_matches_fold_case_and_punctuation) {
  const Index index = make_index();
  CHECK_EQ(index.size(), 9U);
  CHECK_EQ(index.exact("APPLE"), Positions{0});
  CHECK_EQ(index.exact("elder"), Positions{4});
  CHECK_EQ(index.exact("Ice Cream!"), Positions{8});
  CHECK_EQ(index.exact("fig"), (Positions{6, 7}));  // every homograph
  CHECK(index.exact("grape").empty());
  CHECK(index.exact("").empty());
}

TEST(index, case_sensitive_rules_prefer_exact_spelling) {
  const Index index = make_index({.case_sensitive = true, .strip_punctuation = true});
  CHECK_EQ(index.exact("Apple"), Positions{0});
  CHECK_EQ(index.exact("apple"), Positions{0});  // falls back to the folded match
  CHECK_EQ(index.fold("Apple"), "apple");
}

TEST(index, prefix_and_substring_searches_return_distinct_headwords) {
  const Index index = make_index();
  CHECK_EQ(index.prefix("eld", 0), (Positions{4, 5}));
  CHECK_EQ(index.prefix("FIG", 0), Positions{6});  // duplicates collapse
  CHECK_EQ(index.prefix("a", 1).size(), 1U);
  CHECK_EQ(index.containing("err", 0), (Positions{2, 5}));
  CHECK(index.containing("zzz", 0).empty());
}

TEST(index, fuzzy_matches_sort_by_distance) {
  const Index index = make_index();
  const auto close = index.fuzzy("banan", 5);
  REQUIRE(!close.empty());
  CHECK_EQ(close[0].position, 1U);
  CHECK_EQ(close[0].distance, 1U);
  const auto typo = index.fuzzy("chery", 5);
  REQUIRE(!typo.empty());
  CHECK_EQ(typo[0].position, 2U);
  CHECK(index.fuzzy("zzzzzzzz", 5).empty());
}

TEST(index, works_on_the_example_dictionary) {
  const auto path = examples_dir() / "toy.mdx";
  if (!std::filesystem::exists(path)) SKIP("example dictionary not present");
  const auto archive = Archive::open(path);
  REQUIRE(archive.has_value());
  const Index index{archive->keys(), {.case_sensitive = archive->header().case_sensitive, .strip_punctuation = true}};
  CHECK(!index.exact("cake").empty());
  CHECK_EQ(index.exact("CAKE"), index.exact("cake"));
  bool prefixes_ok = true;
  for (const std::uint32_t position : index.prefix("cak", 10)) {
    prefixes_ok = prefixes_ok && index.fold(archive->keys()[position].text).starts_with("cak");
  }
  CHECK(prefixes_ok);
  const auto fuzzy = index.fuzzy("caek", 3);
  REQUIRE(!fuzzy.empty());
  CHECK_EQ(archive->keys()[fuzzy[0].position].text, "cake");
}
