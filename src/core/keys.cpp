#include "minedict/keys.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "minedict/unicode.hpp"

namespace minedict {

std::string normalize_key(std::string_view key, KeyRules rules) {
  std::string out;
  out.reserve(key.size());
  for (std::size_t i = 0; i < key.size();) {
    const auto [cp, n] = unicode::decode_utf8(key, i);
    i += n;
    if (cp == 0) continue;
    if (rules.strip_punctuation && (unicode::is_space(cp) || unicode::is_punctuation(cp))) continue;
    unicode::append_utf8(out, rules.case_sensitive ? cp : unicode::fold_case(cp));
  }
  return out;
}

std::size_t edit_distance(std::string_view a, std::string_view b, std::size_t cap) {
  const std::u32string ua = unicode::to_u32(a);
  const std::u32string ub = unicode::to_u32(b);
  const std::size_t n = ua.size();
  const std::size_t m = ub.size();
  if (n > m + cap || m > n + cap) return cap + 1;
  if (n == 0) return m;
  if (m == 0) return n;

  std::vector<std::size_t> two_back(m + 1);
  std::vector<std::size_t> previous(m + 1);
  std::vector<std::size_t> current(m + 1);
  for (std::size_t j = 0; j <= m; ++j) previous[j] = j;

  for (std::size_t i = 1; i <= n; ++i) {
    current[0] = i;
    std::size_t row_min = i;
    for (std::size_t j = 1; j <= m; ++j) {
      const std::size_t cost = ua[i - 1] == ub[j - 1] ? 0 : 1;
      std::size_t best = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
      if (i > 1 && j > 1 && ua[i - 1] == ub[j - 2] && ua[i - 2] == ub[j - 1]) {
        best = std::min(best, two_back[j - 2] + cost);
      }
      current[j] = best;
      row_min = std::min(row_min, best);
    }
    if (row_min > cap) return cap + 1;
    std::swap(two_back, previous);
    std::swap(previous, current);
  }
  return previous[m];
}

}  // namespace minedict
