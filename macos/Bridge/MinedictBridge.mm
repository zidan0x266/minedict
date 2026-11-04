// minedict - BSD 3-Clause License; see README.
#import "MinedictBridge.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "minedict/cli/dictionary.hpp"
#include "minedict/cli/shelf.hpp"

namespace cli = minedict::cli;
namespace fs = std::filesystem;

NSErrorDomain const MDErrorDomain = @"io.github.zidan0x266.minedict";

namespace {

NSString *to_string(std::string_view text) {
  NSString *utf8 = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
  if (utf8) return utf8;
  // Not valid UTF-8 after all: keep every byte as a Latin-1 character rather than lose the text.
  NSString *latin1 = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSISOLatin1StringEncoding];
  return latin1 ? latin1 : @"";
}

std::string from_string(NSString *text) {
  const char *utf8 = text.UTF8String;
  return utf8 ? std::string(utf8) : std::string();
}

NSArray<NSString *> *to_array(const std::vector<std::string> &items) {
  NSMutableArray<NSString *> *out = [NSMutableArray arrayWithCapacity:items.size()];
  for (const std::string &item : items) [out addObject:to_string(item)];
  return out;
}

NSArray<NSString *> *sorted_for_display(NSArray<NSString *> *words) {
  return [words sortedArrayUsingSelector:@selector(localizedStandardCompare:)];
}

NSError *to_error(const minedict::Error &error) {
  return [NSError errorWithDomain:MDErrorDomain
                             code:static_cast<NSInteger>(error.kind)
                         userInfo:@{NSLocalizedDescriptionKey : to_string(error.message)}];
}

NSString *grouped(std::uint64_t value) {
  return [NSNumberFormatter localizedStringFromNumber:@(value) numberStyle:NSNumberFormatterDecimalStyle];
}

NSString *file_size(std::uint64_t bytes) {
  return [NSByteCountFormatter stringFromByteCount:static_cast<long long>(bytes)
                                        countStyle:NSByteCountFormatterCountStyleFile];
}

}  // namespace

// ---------------------------------------------------------------------------

@interface MDEntry ()
- (instancetype)initWithEntry:(const cli::Entry &)entry;
@end

@implementation MDEntry
- (instancetype)initWithEntry:(const cli::Entry &)entry {
  if ((self = [super init])) {
    _headword = to_string(entry.headword);
    _html = to_string(entry.definition);
    _via = to_array(entry.via);
  }
  return self;
}
@end

// ---------------------------------------------------------------------------

@interface MDDictionary ()
- (instancetype)initWithDictionary:(const cli::Dictionary &)dictionary
                        identifier:(NSString *)identifier
                             shelf:(std::shared_ptr<const cli::Shelf>)shelf;
@property (nonatomic, readonly) const cli::Dictionary *dictionary;
@property (nonatomic, readonly) const cli::Shelf *shelfPointer;
@end

@implementation MDDictionary {
  std::shared_ptr<const cli::Shelf> _shelf;  // keeps the C++ dictionary alive
  const cli::Dictionary *_dictionary;
}

- (instancetype)initWithDictionary:(const cli::Dictionary &)dictionary
                        identifier:(NSString *)identifier
                             shelf:(std::shared_ptr<const cli::Shelf>)shelf {
  if ((self = [super init])) {
    _shelf = std::move(shelf);
    _dictionary = &dictionary;
    _identifier = [identifier copy];
    _title = to_string(dictionary.metadata().title);
    _path = to_string(dictionary.metadata().path.string());
  }
  return self;
}

- (const cli::Dictionary *)dictionary {
  return _dictionary;
}
- (const cli::Shelf *)shelfPointer {
  return _shelf.get();
}
- (BOOL)isResourceFile {
  return _dictionary->metadata().kind == minedict::Kind::mdd;
}
- (NSUInteger)entryCount {
  return static_cast<NSUInteger>(_dictionary->metadata().entries);
}

- (NSArray<NSArray<NSString *> *> *)infoRows {
  const cli::Metadata &meta = _dictionary->metadata();
  const minedict::Header &header = meta.header;
  const minedict::ArchiveStats &stats = _dictionary->archive().stats();
  NSMutableArray<NSArray<NSString *> *> *rows = [NSMutableArray array];
  const auto add = [rows](NSString *label, NSString *value) {
    if (value.length > 0) [rows addObject:@[ label, value ]];
  };
  add(@"Title", to_string(meta.title));
  add(@"File", to_string(meta.path.string()));
  add(@"Size", file_size(meta.file_size));
  add(@"Type", meta.kind == minedict::Kind::mdx ? @"MDX word dictionary" : @"MDD resource file");
  add(@"Engine", to_string(std::format("{} (requires {})", header.version.str(), header.required_version.str())));
  add(@"Encoding", to_string(header.encoding.name));
  add(@"Entries", grouped(meta.entries));
  add(@"Blocks", to_string(std::format("{} key, {} record", stats.key_blocks, stats.record_blocks)));
  if (header.encryption) {
    add(@"Encrypted", header.encryption.key_blocks ? @"key blocks (needs a registration code)" : @"key index");
  }
  add(@"Created", to_string(header.creation_date));
  add(@"Description", to_string(cli::strip_tags(header.description)));
  for (const std::string &warning : _dictionary->archive().warnings()) add(@"Note", to_string(warning));
  return rows;
}

- (NSArray<NSString *> *)stylesheetNames {
  NSMutableArray<NSString *> *names = [NSMutableArray array];
  for (const fs::path &file : _dictionary->stylesheet_files()) [names addObject:to_string(file.filename().string())];
  return names;
}

- (NSArray<MDEntry *> *)lookup:(NSString *)word {
  auto entries = _dictionary->lookup(from_string(word));
  if (!entries) return @[];
  NSMutableArray<MDEntry *> *out = [NSMutableArray arrayWithCapacity:entries->size()];
  for (const cli::Entry &entry : *entries) [out addObject:[[MDEntry alloc] initWithEntry:entry]];
  return out;
}

- (NSArray<NSString *> *)headwordsWithPrefix:(NSString *)prefix limit:(NSUInteger)limit {
  NSMutableArray<NSString *> *out = [NSMutableArray array];
  for (const cli::KeyRef &key : _dictionary->find_prefix(from_string(prefix), limit)) [out addObject:to_string(key.key)];
  return out;
}

- (NSArray<NSString *> *)headwordsContaining:(NSString *)text limit:(NSUInteger)limit {
  NSMutableArray<NSString *> *out = [NSMutableArray array];
  for (const cli::KeyRef &key : _dictionary->find_containing(from_string(text), limit)) {
    [out addObject:to_string(key.key)];
  }
  return out;
}
@end

// ---------------------------------------------------------------------------

@interface MDHit ()
- (instancetype)initWithDictionary:(MDDictionary *)dictionary entries:(NSArray<MDEntry *> *)entries;
@end

@implementation MDHit
- (instancetype)initWithDictionary:(MDDictionary *)dictionary entries:(NSArray<MDEntry *> *)entries {
  if ((self = [super init])) {
    _dictionary = dictionary;
    _entries = [entries copy];
  }
  return self;
}
@end

// ---------------------------------------------------------------------------

@implementation MDShelf {
  std::shared_ptr<cli::Shelf> _shelf;
  NSMutableArray<MDDictionary *> *_dictionaries;
  NSMutableArray<NSString *> *_problems;
}

- (instancetype)init {
  if ((self = [super init])) {
    _shelf = std::make_shared<cli::Shelf>();
    _dictionaries = [NSMutableArray array];
    _problems = [NSMutableArray array];
  }
  return self;
}

- (BOOL)addPath:(NSString *)path error:(NSError **)error {
  const std::size_t dictionaries_before = _shelf->size();
  const std::size_t problems_before = _shelf->problems().size();
  const auto added = _shelf->add(fs::path(from_string(path)));
  for (const std::string &problem : _shelf->problems() | std::views::drop(problems_before)) {
    [_problems addObject:to_string(problem)];
  }
  if (!added) {
    if (error) *error = to_error(added.error());
    return NO;
  }
  for (std::size_t i = dictionaries_before; i < _shelf->size(); ++i) {
    [_dictionaries addObject:[[MDDictionary alloc] initWithDictionary:_shelf->at(i)
                                                           identifier:[NSString stringWithFormat:@"d%zu", i]
                                                                shelf:_shelf]];
  }
  return YES;
}

- (NSArray<MDDictionary *> *)dictionaries {
  return [_dictionaries copy];
}
- (NSArray<NSString *> *)problems {
  return [_problems copy];
}

- (nullable MDDictionary *)dictionaryWithIdentifier:(NSString *)identifier {
  for (MDDictionary *dictionary in _dictionaries) {
    if ([dictionary.identifier isEqualToString:identifier]) return dictionary;
  }
  return nil;
}

- (nullable MDDictionary *)wrapperFor:(const cli::Dictionary *)dictionary {
  for (MDDictionary *wrapper in _dictionaries) {
    if (wrapper.dictionary == dictionary) return wrapper;
  }
  return nil;
}

- (NSArray<MDHit *> *)lookup:(NSString *)word {
  NSMutableArray<MDHit *> *hits = [NSMutableArray array];
  for (const cli::Shelf::Hit &hit : _shelf->lookup(from_string(word)).hits) {
    MDDictionary *dictionary = [self wrapperFor:hit.dictionary];
    if (!dictionary) continue;
    NSMutableArray<MDEntry *> *entries = [NSMutableArray arrayWithCapacity:hit.entries.size()];
    for (const cli::Entry &entry : hit.entries) [entries addObject:[[MDEntry alloc] initWithEntry:entry]];
    [hits addObject:[[MDHit alloc] initWithDictionary:dictionary entries:entries]];
  }
  return hits;
}

- (NSArray<NSString *> *)headwordsWithPrefix:(NSString *)prefix limit:(NSUInteger)limit {
  return sorted_for_display(to_array(_shelf->complete(from_string(prefix), limit)));
}

- (NSArray<NSString *> *)headwordsContaining:(NSString *)text limit:(NSUInteger)limit {
  NSMutableOrderedSet<NSString *> *unique = [NSMutableOrderedSet orderedSet];
  for (const cli::Shelf::Match &match : _shelf->find_containing(from_string(text), limit)) {
    [unique addObject:to_string(match.key.key)];
  }
  NSArray<NSString *> *words = unique.array;
  if (limit != 0 && words.count > limit) words = [words subarrayWithRange:NSMakeRange(0, limit)];
  return sorted_for_display(words);
}

- (NSArray<NSString *> *)suggestionsForWord:(NSString *)word limit:(NSUInteger)limit {
  NSMutableArray<NSString *> *out = [NSMutableArray array];
  for (const cli::Shelf::Candidate &candidate : _shelf->suggest(from_string(word), limit)) {
    [out addObject:to_string(candidate.suggestion.key)];
  }
  return out;
}

- (nullable NSData *)resourceNamed:(NSString *)name
                     forDictionary:(MDDictionary *)dictionary
                          mimeType:(NSString *_Nullable *_Nullable)mimeType {
  if (dictionary.shelfPointer != _shelf.get()) return nil;
  const std::string wanted = from_string(name);
  if (wanted.empty()) return nil;
  if (mimeType) *mimeType = to_string(cli::mime_type_for(wanted));

  const cli::Dictionary &owner = *dictionary.dictionary;
  const fs::path folder = owner.metadata().path.parent_path();
  const std::string stem = owner.metadata().path.stem().string();

  // The .mdd files that share the dictionary's name first, then the rest of its folder.
  std::vector<const cli::Dictionary *> resources;
  if (owner.metadata().kind == minedict::Kind::mdd) resources.push_back(&owner);
  for (const cli::Dictionary &candidate : _shelf->dictionaries()) {
    if (candidate.metadata().kind != minedict::Kind::mdd || &candidate == &owner) continue;
    if (candidate.metadata().path.parent_path() != folder) continue;
    resources.push_back(&candidate);
  }
  std::ranges::stable_partition(resources, [&stem](const cli::Dictionary *candidate) {
    return candidate->metadata().path.stem().string().starts_with(stem);
  });
  for (const cli::Dictionary *resource : resources) {
    if (const auto bytes = resource->resource(wanted)) {
      return [NSData dataWithBytes:bytes->data() length:bytes->size()];
    }
  }

  // Then a plain file beside the dictionary (CSS, JavaScript, fonts), kept inside its folder.
  std::string relative = wanted;
  std::ranges::replace(relative, '\\', '/');
  while (relative.starts_with('/')) relative.erase(0, 1);
  std::error_code ec;
  const fs::path root = fs::weakly_canonical(folder, ec);
  if (ec) return nil;
  const fs::path file = fs::weakly_canonical(folder / relative, ec);
  if (ec) return nil;
  const std::string inside = file.lexically_relative(root).string();
  if (inside.empty() || inside == "." || inside.starts_with("..")) return nil;
  if (!fs::is_regular_file(file, ec)) return nil;
  return [NSData dataWithContentsOfFile:to_string(file.string())];
}

+ (NSArray<NSString *> *)defaultSources {
  NSMutableArray<NSString *> *out = [NSMutableArray array];
  for (const fs::path &source : cli::Shelf::default_sources()) [out addObject:to_string(source.string())];
  return out;
}
@end
