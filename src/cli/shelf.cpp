// minedict - BSD 3-Clause License; see README.
#include "minedict/cli/shelf.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <future>
#include <iterator>
#include <set>

#include "minedict/cli/terminal.hpp"
#include "minedict/cli/text.hpp"

namespace minedict::cli {
namespace {

bool has_extension(const std::filesystem::path& path, std::string_view wanted) {
  return text::to_lower_ascii(path.extension().string()) == wanted;
}

/// Resource files (.mdd) hold paths like `\\pic\\cake.png`; they only take part
/// in searches that look like one.
bool looks_like_resource(std::string_view query) noexcept {
  return query.find_first_of("\\/.") != std::string_view::npos;
}

bool participates(const Dictionary& dictionary, std::string_view query) noexcept {
  return dictionary.metadata().kind == Kind::mdx || looks_like_resource(query);
}

}  // namespace

Result<void> Shelf::add(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec)) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
      if (!entry.is_regular_file(ec)) continue;
      if (has_extension(entry.path(), ".mdx") || has_extension(entry.path(), ".mdd")) files.push_back(entry.path());
    }
    if (std::ranges::none_of(files, [](const auto& file) { return has_extension(file, ".mdx"); })) {
      return fail(ErrorKind::not_found, std::format("{}: no .mdx dictionaries in this directory", path.string()));
    }
    // Word dictionaries first, then their resource files, each group by name.
    std::ranges::sort(files, {}, [](const std::filesystem::path& file) {
      return std::pair(!has_extension(file, ".mdx"), file.filename().string());
    });
    std::vector<std::future<Result<Dictionary>>> opening;
    for (const auto& file : files) {
      opening.push_back(std::async(std::launch::async, [file] { return Dictionary::open(file); }));
    }
    std::vector<std::string> failures;
    for (auto& future : opening) {
      auto dictionary = future.get();
      if (!dictionary) {
        failures.push_back(dictionary.error().message);
        continue;
      }
      dictionaries_.push_back(std::make_unique<Dictionary>(std::move(*dictionary)));
    }
    if (failures.size() == files.size()) {
      return fail(ErrorKind::format, failures.front());
    }
    problems_.insert(problems_.end(), std::make_move_iterator(failures.begin()),
                     std::make_move_iterator(failures.end()));
    return {};
  }

  auto dictionary = Dictionary::open(path);
  if (!dictionary) return std::unexpected(dictionary.error());
  dictionaries_.push_back(std::make_unique<Dictionary>(std::move(*dictionary)));

  // A word dictionary brings the resource files that share its name
  // (`oxford.mdd`, `oxford.1.mdd`, ...) unless they are already open.
  if (has_extension(path, ".mdx")) {
    const std::string stem = path.stem().string();
    std::vector<std::filesystem::path> companions;
    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path(), ec)) {
      if (!entry.is_regular_file(ec) || !has_extension(entry.path(), ".mdd")) continue;
      if (!entry.path().stem().string().starts_with(stem)) continue;
      const bool open = std::ranges::any_of(dictionaries_, [&](const auto& owned) {
        return std::filesystem::equivalent(owned->metadata().path, entry.path(), ec);
      });
      if (!open) companions.push_back(entry.path());
    }
    std::ranges::sort(companions);
    for (const auto& companion : companions) {
      auto resources = Dictionary::open(companion);
      if (!resources) {
        problems_.push_back(std::move(resources.error().message));
        continue;
      }
      dictionaries_.push_back(std::make_unique<Dictionary>(std::move(*resources)));
    }
  }
  return {};
}

Shelf::LookupOutcome Shelf::lookup(std::string_view word, const LookupOptions& options) const {
  LookupOutcome outcome;
  for (const auto& dictionary : dictionaries_) {
    if (dictionary->metadata().kind == Kind::mdd) continue;  // resources, not words
    auto entries = dictionary->lookup(word, options);
    if (!entries) {
      outcome.problems.push_back(std::move(entries.error()));
      continue;
    }
    if (!entries->empty()) {
      outcome.hits.push_back(Hit{.dictionary = dictionary.get(), .entries = std::move(*entries)});
    }
  }
  return outcome;
}

std::vector<Shelf::Match> Shelf::find_prefix(std::string_view prefix, std::size_t limit) const {
  std::vector<Match> matches;
  for (const auto& dictionary : dictionaries_) {
    if (!participates(*dictionary, prefix)) continue;
    for (const KeyRef& key : dictionary->find_prefix(prefix, limit)) {
      matches.push_back(Match{.dictionary = dictionary.get(), .key = key});
    }
  }
  return matches;
}

std::vector<Shelf::Match> Shelf::find_containing(std::string_view needle, std::size_t limit) const {
  std::vector<Match> matches;
  for (const auto& dictionary : dictionaries_) {
    if (!participates(*dictionary, needle)) continue;
    for (const KeyRef& key : dictionary->find_containing(needle, limit)) {
      matches.push_back(Match{.dictionary = dictionary.get(), .key = key});
    }
  }
  return matches;
}

std::vector<Shelf::Candidate> Shelf::suggest(std::string_view word, std::size_t limit) const {
  std::vector<Candidate> candidates;
  std::set<std::string> seen;
  for (const auto& dictionary : dictionaries_) {
    if (!participates(*dictionary, word)) continue;
    for (Suggestion& suggestion : dictionary->suggest(word, limit)) {
      if (!seen.insert(normalize_key(suggestion.key)).second) continue;
      candidates.push_back(Candidate{.dictionary = dictionary.get(), .suggestion = std::move(suggestion)});
    }
  }
  std::ranges::stable_sort(candidates, {}, [](const Candidate& c) {
    return std::pair(c.suggestion.distance, c.suggestion.key.size());
  });
  if (limit != 0 && candidates.size() > limit) candidates.resize(limit);
  return candidates;
}

std::vector<std::string> Shelf::complete(std::string_view prefix, std::size_t limit) const {
  std::set<std::string> unique;
  for (const auto& dictionary : dictionaries_) {
    if (!participates(*dictionary, prefix)) continue;
    for (const KeyRef& key : dictionary->find_prefix(prefix, limit)) unique.emplace(key.key);
  }
  std::vector<std::string> out(unique.begin(), unique.end());
  if (limit != 0 && out.size() > limit) out.resize(limit);
  return out;
}

std::vector<std::filesystem::path> Shelf::default_sources() {
  std::vector<std::filesystem::path> sources;

  if (const auto env = environment("MINEDICT_DICTS"); env && !env->empty()) {
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    for (const auto part : std::views::split(std::string_view{*env}, separator)) {
      const std::string_view item = text::trim(std::string_view{part.begin(), part.end()});
      if (!item.empty()) sources.push_back(expand_tilde(item));
    }
  }

  const std::filesystem::path config = config_directory() / "dictionaries";
  if (std::ifstream in(config); in) {
    std::string line;
    while (std::getline(in, line)) {
      const std::string_view item = text::trim(line);
      if (item.empty() || item.starts_with('#')) continue;
      sources.push_back(expand_tilde(item));
    }
  }
  return sources;
}

}  // namespace minedict::cli
