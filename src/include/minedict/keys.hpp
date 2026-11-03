// minedict - how headwords are compared.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace minedict {

/// Mirrors the header flags `KeyCaseSensitive` and `StripKey`.
struct KeyRules {
  bool case_sensitive{false};
  bool strip_punctuation{true};

  [[nodiscard]] bool operator==(const KeyRules&) const = default;
};

/// Canonical form of a headword under `rules`: NULs dropped, optionally
/// case-folded and stripped of spaces and punctuation.
[[nodiscard]] std::string normalize_key(std::string_view key, KeyRules rules = {});

/// Optimal string alignment distance over code points (insert, delete,
/// substitute, transpose). Gives up once the distance must exceed `cap`
/// and returns `cap + 1`.
[[nodiscard]] std::size_t edit_distance(std::string_view a, std::string_view b, std::size_t cap);

}  // namespace minedict
