// minedict - BSD 3-Clause License; see README.
//
// A shelf holds every dictionary the user opened and answers questions
// across all of them.
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "minedict/cli/dictionary.hpp"
#include "minedict/error.hpp"

namespace minedict::cli {

class Shelf {
 public:
  struct Hit {
    const Dictionary* dictionary;
    std::vector<Entry> entries;
  };
  struct LookupOutcome {
    std::vector<Hit> hits;
    std::vector<Error> problems;
    [[nodiscard]] bool found() const noexcept { return !hits.empty(); }
  };
  struct Match {
    const Dictionary* dictionary;
    KeyRef key;
  };
  struct Candidate {
    const Dictionary* dictionary;
    Suggestion suggestion;
  };

  /// Open a dictionary file, or every `.mdx` inside a directory.
  [[nodiscard]] Result<void> add(const std::filesystem::path& path);

  [[nodiscard]] auto dictionaries() const noexcept {
    return dictionaries_ | std::views::transform([](const auto& owned) -> const Dictionary& { return *owned; });
  }
  [[nodiscard]] const Dictionary& at(std::size_t index) const noexcept { return *dictionaries_[index]; }
  [[nodiscard]] std::size_t size() const noexcept { return dictionaries_.size(); }
  [[nodiscard]] bool empty() const noexcept { return dictionaries_.empty(); }

  [[nodiscard]] LookupOutcome lookup(std::string_view word, const LookupOptions& options = {}) const;
  [[nodiscard]] std::vector<Match> find_prefix(std::string_view prefix, std::size_t limit) const;
  [[nodiscard]] std::vector<Match> find_containing(std::string_view needle, std::size_t limit) const;
  [[nodiscard]] std::vector<Candidate> suggest(std::string_view word, std::size_t limit) const;

  /// Distinct headwords starting with `prefix`, for tab completion.
  [[nodiscard]] std::vector<std::string> complete(std::string_view prefix, std::size_t limit) const;

  /// Dictionaries named by `$MINEDICT_DICTS` and the user's config file.
  [[nodiscard]] static std::vector<std::filesystem::path> default_sources();

 private:
  std::vector<std::unique_ptr<Dictionary>> dictionaries_;
};

}  // namespace minedict::cli
