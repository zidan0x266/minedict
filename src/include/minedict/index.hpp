// minedict - a search index over an archive's headwords.
//
// Keys are normalised (case-folded, punctuation stripped as the header
// requests) and sorted once; exact, prefix, substring and fuzzy queries
// then run over that table and answer with key positions.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "minedict/archive.hpp"
#include "minedict/keys.hpp"

namespace minedict {

struct FuzzyMatch {
  std::uint32_t position;
  std::size_t distance;
};

class Index {
 public:
  Index() = default;

  /// Build the index for `keys`, which must outlive it.
  Index(std::span<const KeyEntry> keys, KeyRules rules);

  [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
  [[nodiscard]] KeyRules rules() const noexcept { return rules_; }

  /// The comparison form of a word (always case-insensitive; the
  /// case-sensitive filter is applied by `exact`).
  [[nodiscard]] std::string fold(std::string_view word) const;

  /// Positions of every key equal to `word` under the rules, in file order.
  [[nodiscard]] std::vector<std::uint32_t> exact(std::string_view word) const;

  /// Distinct headwords starting with `prefix`, in index order. `limit` 0 = all.
  [[nodiscard]] std::vector<std::uint32_t> prefix(std::string_view prefix, std::size_t limit) const;

  /// Distinct headwords containing `needle` anywhere.
  [[nodiscard]] std::vector<std::uint32_t> containing(std::string_view needle, std::size_t limit) const;

  /// Distinct headwords within `max_distance` edits of `word`, closest first.
  [[nodiscard]] std::vector<FuzzyMatch> fuzzy(std::string_view word, std::size_t limit,
                                              std::size_t max_distance = 2) const;

 private:
  struct Item {
    std::string folded;
    std::uint32_t position;
  };

  [[nodiscard]] std::span<const Item> equal_range(std::string_view folded) const;

  std::span<const KeyEntry> keys_;
  std::vector<Item> items_;  // sorted by (folded, position)
  KeyRules rules_;
  KeyRules folded_rules_;
};

}  // namespace minedict
