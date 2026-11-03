// minedict - a dictionary as the terminal sees it: one MDX or MDD archive,
// its search index, the entries it yields and the CSS that styles them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "minedict/archive.hpp"
#include "minedict/cli/css.hpp"
#include "minedict/error.hpp"
#include "minedict/index.hpp"

namespace minedict::cli {

struct Metadata {
  Kind kind{Kind::mdx};
  std::filesystem::path path;
  std::uint64_t file_size{0};
  std::uint64_t entries{0};
  std::string title;  // plain text: the header's title without tags, else the file name
  Header header;      // everything the file says about itself
};

/// A headword as stored in the file, by its position in the key list.
struct KeyRef {
  std::string_view key;
  std::uint32_t position{0};
  std::uint64_t record_offset{0};
};

struct Entry {
  std::string headword;
  std::string definition;        // HTML (MDX)
  std::vector<std::string> via;  // headwords followed through @@@LINK redirects
};

struct LookupOptions {
  bool follow_links{true};
  unsigned max_hops{5};
};

struct Suggestion {
  std::string key;
  std::size_t distance{0};
  std::uint32_t position{0};
};

inline constexpr std::string_view link_prefix{"@@@LINK="};

class Dictionary {
 public:
  [[nodiscard]] static Result<Dictionary> open(const std::filesystem::path& path);

  Dictionary(Dictionary&&) noexcept;
  Dictionary& operator=(Dictionary&&) noexcept;
  Dictionary(const Dictionary&) = delete;
  Dictionary& operator=(const Dictionary&) = delete;
  ~Dictionary();

  [[nodiscard]] const Metadata& metadata() const noexcept;
  [[nodiscard]] const Archive& archive() const noexcept;
  /// The search index, built on first use.
  [[nodiscard]] const Index& index() const;
  [[nodiscard]] KeyRules key_rules() const noexcept;

  /// Number of headwords, duplicates included.
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] KeyRef key_at(std::uint32_t position) const;

  /// The definition HTML stored for a headword position, with the header's
  /// `StyleSheet` markers expanded.
  [[nodiscard]] Result<std::string> definition_at(std::uint32_t position) const;

  /// Every entry matching `word` under the dictionary's key rules,
  /// following `@@@LINK=` redirects when asked to.
  [[nodiscard]] Result<std::vector<Entry>> lookup(std::string_view word, const LookupOptions& options = {}) const;

  [[nodiscard]] std::vector<KeyRef> find_prefix(std::string_view prefix, std::size_t limit) const;
  [[nodiscard]] std::vector<KeyRef> find_containing(std::string_view needle, std::size_t limit) const;

  /// Headwords within `max_distance` edits of `word`, closest first.
  [[nodiscard]] std::vector<Suggestion> suggest(std::string_view word, std::size_t limit,
                                                std::size_t max_distance = 2) const;

  /// Raw bytes of an MDD resource such as `\pic\word.png` (slashes either way).
  [[nodiscard]] Result<std::vector<std::byte>> resource(std::string_view name) const;

  /// The dictionary's own CSS (`<stem>.css` beside the file, or every `.css`
  /// in its folder), parsed on first use. Null when there is none.
  [[nodiscard]] const css::Sheet* stylesheet() const;
  [[nodiscard]] const std::vector<std::filesystem::path>& stylesheet_files() const;

 private:
  struct Impl;
  explicit Dictionary(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

/// Drop tags and entities from a one-line HTML title.
[[nodiscard]] std::string strip_tags(std::string_view html);

/// Guess a MIME type from a resource name's extension.
[[nodiscard]] std::string_view mime_type_for(std::string_view name) noexcept;

}  // namespace minedict::cli
