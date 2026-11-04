// minedict - BSD 3-Clause License; see README.
//
// The Objective-C bridge the macOS app is built on, exercised on the example dictionary.
#import "MinedictBridge.h"

#include <stdexcept>
#include <string>

#include "check.hpp"

namespace {

std::string utf8(NSString *text) { return text.UTF8String ? std::string(text.UTF8String) : std::string(); }

bool contains(NSArray<NSString *> *array, const char *wanted) {
  return [array containsObject:[NSString stringWithUTF8String:wanted]];
}

NSString *examples() { return [NSString stringWithUTF8String:MINEDICT_EXAMPLES_DIR]; }

MDShelf *example_shelf() {
  static MDShelf *shelf = [] {
    MDShelf *s = [[MDShelf alloc] init];
    NSError *error = nil;
    if (![s addPath:[examples() stringByAppendingPathComponent:@"toy.mdx"] error:&error]) {
      throw std::runtime_error(utf8(error.localizedDescription));
    }
    return s;
  }();
  return shelf;
}

}  // namespace

TEST(bridge, opens_the_example) {
  MDShelf *shelf = example_shelf();
  REQUIRE_EQ(shelf.dictionaries.count, 1u);
  MDDictionary *toy = shelf.dictionaries.firstObject;
  CHECK_EQ(utf8(toy.title), "Toy English-Chinese Dictionary");
  CHECK_EQ(utf8(toy.identifier), "d0");
  CHECK_FALSE(toy.isResourceFile);
  CHECK(toy.entryCount > 2500);
  CHECK(shelf.problems.count == 0);
  CHECK([shelf dictionaryWithIdentifier:@"d0"] == toy);
  CHECK([shelf dictionaryWithIdentifier:@"d9"] == nil);
}

TEST(bridge, opens_a_folder) {
  MDShelf *shelf = [[MDShelf alloc] init];
  NSError *error = nil;
  REQUIRE([shelf addPath:examples() error:&error]);
  CHECK_EQ(shelf.dictionaries.count, 1u);
  CHECK(error == nil);
}

TEST(bridge, reports_files_it_cannot_open) {
  MDShelf *shelf = [[MDShelf alloc] init];
  NSError *error = nil;
  CHECK_FALSE([shelf addPath:@"/nonexistent/words.mdx" error:&error]);
  REQUIRE(error != nil);
  CHECK(utf8(error.domain) == utf8(MDErrorDomain));
  CHECK(utf8(error.localizedDescription).find("no such file") != std::string::npos);
  CHECK_EQ(shelf.dictionaries.count, 0u);
}

TEST(bridge, looks_up_words) {
  MDShelf *shelf = example_shelf();
  NSArray<MDHit *> *hits = [shelf lookup:@"cake"];
  REQUIRE_EQ(hits.count, 1u);
  MDHit *hit = hits.firstObject;
  CHECK(hit.dictionary == shelf.dictionaries.firstObject);
  REQUIRE_EQ(hit.entries.count, 1u);
  MDEntry *entry = hit.entries.firstObject;
  CHECK_EQ(utf8(entry.headword), "cake");
  CHECK(utf8(entry.html).starts_with("<div class=\"entry\">"));
  CHECK(utf8(entry.html).find("\xe8\x9b\x8b\xe7\xb3\x95") != std::string::npos);  // the Chinese gloss
  CHECK_EQ(entry.via.count, 0u);

  CHECK_EQ([shelf lookup:@"CAKE"].count, 1u);  // keys are case-insensitive
  CHECK_EQ([shelf lookup:@"no such word"].count, 0u);
  CHECK_EQ([shelf.dictionaries.firstObject lookup:@"cake"].count, 1u);
}

TEST(bridge, searches_headwords) {
  MDShelf *shelf = example_shelf();
  NSArray<NSString *> *prefixed = [shelf headwordsWithPrefix:@"ca" limit:50];
  CHECK(contains(prefixed, "cake"));
  CHECK(prefixed.count <= 50);
  CHECK([prefixed isEqualToArray:[prefixed sortedArrayUsingSelector:@selector(localizedStandardCompare:)]]);

  NSArray<NSString *> *containing = [shelf headwordsContaining:@"ake" limit:50];
  CHECK(contains(containing, "cake"));
  CHECK(contains(containing, "lake"));

  CHECK_EQ([shelf headwordsWithPrefix:@"zzzz" limit:10].count, 0u);
  CHECK(contains([shelf suggestionsForWord:@"caek" limit:5], "cake"));
}

TEST(bridge, describes_a_dictionary) {
  MDDictionary *toy = example_shelf().dictionaries.firstObject;
  bool saw_type = false;
  bool saw_entries = false;
  for (NSArray<NSString *> *row in toy.infoRows) {
    REQUIRE_EQ(row.count, 2u);
    if ([row[0] isEqualToString:@"Type"]) saw_type = [row[1] isEqualToString:@"MDX word dictionary"];
    if ([row[0] isEqualToString:@"Entries"]) saw_entries = row[1].length > 0;
  }
  CHECK(saw_type);
  CHECK(saw_entries);
  CHECK([toy.stylesheetNames isEqualToArray:@[ @"theme.css" ]]);
}

TEST(bridge, serves_files_beside_the_dictionary) {
  MDShelf *shelf = example_shelf();
  MDDictionary *toy = shelf.dictionaries.firstObject;
  NSString *mime = nil;
  NSData *css = [shelf resourceNamed:@"theme.css" forDictionary:toy mimeType:&mime];
  REQUIRE(css != nil);
  CHECK_EQ(utf8(mime), "text/css");
  CHECK(css.length > 100);

  CHECK([shelf resourceNamed:@"missing.png" forDictionary:toy mimeType:nullptr] == nil);
  CHECK([shelf resourceNamed:@"../README.md" forDictionary:toy mimeType:nullptr] == nil);  // stays in the folder
  CHECK([shelf resourceNamed:@"" forDictionary:toy mimeType:nullptr] == nil);

  // A dictionary from another shelf is not served.
  MDShelf *other = [[MDShelf alloc] init];
  REQUIRE([other addPath:[examples() stringByAppendingPathComponent:@"toy.mdx"] error:nullptr]);
  CHECK([shelf resourceNamed:@"theme.css" forDictionary:other.dictionaries.firstObject mimeType:nullptr] == nil);
}

TEST(bridge, lists_default_sources) {
  NSArray<NSString *> *sources = [MDShelf defaultSources];
  CHECK(sources != nil);
}
