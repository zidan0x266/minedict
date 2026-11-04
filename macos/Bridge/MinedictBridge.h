// minedict - BSD 3-Clause License; see README.
//
// The Objective-C face of minedict::cli for the macOS app: dictionaries,
// lookups and resources, with no C++ types crossing into Swift.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// One definition of a headword: the stored HTML with the header's
/// `StyleSheet` markers expanded, after following `@@@LINK=` redirects.
@interface MDEntry : NSObject
@property (nonatomic, readonly, copy) NSString *headword;
@property (nonatomic, readonly, copy) NSString *html;
/// The headwords a redirect passed through on the way to this entry.
@property (nonatomic, readonly, copy) NSArray<NSString *> *via;
@end

/// One MDX word file or MDD resource file, owned by the shelf it was opened on.
@interface MDDictionary : NSObject
/// A short name unique within the shelf, used in `minedict://` URLs.
@property (nonatomic, readonly, copy) NSString *identifier;
@property (nonatomic, readonly, copy) NSString *title;
@property (nonatomic, readonly, copy) NSString *path;
@property (nonatomic, readonly) BOOL isResourceFile;
@property (nonatomic, readonly) NSUInteger entryCount;
/// Label and value pairs describing the file: type, engine, encoding, entries, blocks, ...
@property (nonatomic, readonly, copy) NSArray<NSArray<NSString *> *> *infoRows;
/// The `.css` files beside the dictionary that style its entries, by file name.
@property (nonatomic, readonly, copy) NSArray<NSString *> *stylesheetNames;

- (NSArray<MDEntry *> *)lookup:(NSString *)word;
- (NSArray<NSString *> *)headwordsWithPrefix:(NSString *)prefix limit:(NSUInteger)limit;
- (NSArray<NSString *> *)headwordsContaining:(NSString *)text limit:(NSUInteger)limit;
@end

/// The entries one dictionary has for a word.
@interface MDHit : NSObject
@property (nonatomic, readonly, strong) MDDictionary *dictionary;
@property (nonatomic, readonly, copy) NSArray<MDEntry *> *entries;
@end

FOUNDATION_EXPORT NSErrorDomain const MDErrorDomain;

/// Every dictionary the user opened. Add paths first, then query from any thread.
@interface MDShelf : NSObject
/// Open a `.mdx`/`.mdd` file, or every one inside a folder. Files in a folder
/// that fail to open are listed in `problems`; the call fails only when
/// nothing could be opened.
- (BOOL)addPath:(NSString *)path error:(NSError **)error;
@property (nonatomic, readonly, copy) NSArray<MDDictionary *> *dictionaries;
@property (nonatomic, readonly, copy) NSArray<NSString *> *problems;
- (nullable MDDictionary *)dictionaryWithIdentifier:(NSString *)identifier;

- (NSArray<MDHit *> *)lookup:(NSString *)word;
/// Distinct headwords of the word dictionaries starting with `prefix`, sorted for display.
- (NSArray<NSString *> *)headwordsWithPrefix:(NSString *)prefix limit:(NSUInteger)limit;
- (NSArray<NSString *> *)headwordsContaining:(NSString *)text limit:(NSUInteger)limit;
/// Headwords close to `word` in spelling, closest first.
- (NSArray<NSString *> *)suggestionsForWord:(NSString *)word limit:(NSUInteger)limit;
/// A resource (`pic/cake.png`, `cake.mp3`, `oald.css`) for one dictionary: from
/// the `.mdd` files that share its name, then the other `.mdd` files in its
/// folder, then plain files beside it. `mimeType` receives a guess from the extension.
- (nullable NSData *)resourceNamed:(NSString *)name
                     forDictionary:(MDDictionary *)dictionary
                          mimeType:(NSString *_Nullable *_Nullable)mimeType;

/// Dictionaries named by `$MINEDICT_DICTS` and `~/.config/minedict/dictionaries`.
+ (NSArray<NSString *> *)defaultSources;
@end

NS_ASSUME_NONNULL_END
