#include "minedict/index.hpp"

#include <algorithm>
#include <ranges>
#include <set>
#include <tuple>
#include <utility>

namespace minedict {

Index::Index(std::span<const KeyEntry> keys, KeyRules rules)
    : keys_(keys), rules_(rules), folded_rules_{.case_sensitive = false, .strip_punctuation = rules.strip_punctuation} {
  items_.reserve(keys.size());
  for (std::uint32_t position = 0; position < keys.size(); ++position) {
    items_.push_back(Item{.folded = normalize_key(keys[position].text, folded_rules_), .position = position});
  }
  std::ranges::sort(items_, {}, [](const Item& item) { return std::tie(item.folded, item.position); });
}

std::string Index::fold(std::string_view word) const { return normalize_key(word, folded_rules_); }

std::span<const Index::Item> Index::equal_range(std::string_view folded) const {
  const auto [first, last] = std::ranges::equal_range(items_, folded, {}, &Item::folded);
  return {first, last};
}

std::vector<std::uint32_t> Index::exact(std::string_view word) const {
  std::vector<std::uint32_t> positions;
  const std::string folded = fold(word);
  if (folded.empty()) return positions;
  const auto range = equal_range(folded);
  if (rules_.case_sensitive) {
    for (const Item& item : range) {
      if (keys_[item.position].text == word) positions.push_back(item.position);
    }
  }
  if (positions.empty()) {
    for (const Item& item : range) positions.push_back(item.position);
  }
  std::ranges::sort(positions);
  return positions;
}

std::vector<std::uint32_t> Index::prefix(std::string_view prefix, std::size_t limit) const {
  std::vector<std::uint32_t> out;
  const std::string folded = fold(prefix);
  if (folded.empty()) return out;
  std::set<std::string_view> seen;
  for (auto it = std::ranges::lower_bound(items_, folded, {}, &Item::folded);
       it != items_.end() && it->folded.starts_with(folded); ++it) {
    if (!seen.insert(keys_[it->position].text).second) continue;
    out.push_back(it->position);
    if (limit != 0 && out.size() >= limit) break;
  }
  return out;
}

std::vector<std::uint32_t> Index::containing(std::string_view needle, std::size_t limit) const {
  std::vector<std::uint32_t> out;
  const std::string folded = fold(needle);
  if (folded.empty()) return out;
  std::set<std::string_view> seen;
  for (const Item& item : items_) {
    if (!item.folded.contains(folded)) continue;
    if (!seen.insert(keys_[item.position].text).second) continue;
    out.push_back(item.position);
    if (limit != 0 && out.size() >= limit) break;
  }
  return out;
}

std::vector<FuzzyMatch> Index::fuzzy(std::string_view word, std::size_t limit, std::size_t max_distance) const {
  std::vector<FuzzyMatch> found;
  const std::string folded = fold(word);
  if (folded.empty()) return found;
  std::set<std::string_view> seen;
  for (const Item& item : items_) {
    // A byte-length filter before the real distance (UTF-8 is at most 4 bytes per code point).
    if (item.folded.size() + max_distance * 4 < folded.size() || folded.size() + max_distance * 4 < item.folded.size()) {
      continue;
    }
    const std::size_t distance = edit_distance(folded, item.folded, max_distance);
    if (distance > max_distance) continue;
    if (!seen.insert(keys_[item.position].text).second) continue;
    found.push_back(FuzzyMatch{.position = item.position, .distance = distance});
  }
  std::ranges::stable_sort(found, {}, [this](const FuzzyMatch& m) {
    return std::pair(m.distance, keys_[m.position].text.size());
  });
  if (limit != 0 && found.size() > limit) found.resize(limit);
  return found;
}

}  // namespace minedict
