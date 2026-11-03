// minedict - BSD 3-Clause License; see README.
#include "minedict/cli/presenter.hpp"

#include <algorithm>
#include <format>
#include <utility>

#include "minedict/cli/text.hpp"

namespace minedict::cli {
namespace {

constexpr std::size_t kMinimumWidth = 20;

template <class Range, class Project>
std::string join(const Range& range, std::string_view separator, Project&& project) {
  std::string out;
  bool first = true;
  for (const auto& item : range) {
    if (!std::exchange(first, false)) out += separator;
    out += project(item);
  }
  return out;
}

}  // namespace

Presenter::Presenter(const Theme& theme, ColorMode color, std::size_t width) noexcept
    : theme_(&theme), color_(color), width_(std::max(width, kMinimumWidth)) {}

void Presenter::set_width(std::size_t width) noexcept { width_ = std::max(width, kMinimumWidth); }

HtmlRenderer Presenter::renderer(std::size_t indent, bool show_images, bool show_sounds) const noexcept {
  return HtmlRenderer{*theme_, RenderOptions{.width = width_,
                                             .indent = indent,
                                             .color = color_,
                                             .show_images = show_images,
                                             .show_sounds = show_sounds}};
}

std::string Presenter::paint(std::string_view s, Role role) const { return cli::paint(s, (*theme_)[role], color_); }

std::string Presenter::rule() const { return paint(text::repeat("─", width_), Role::rule); }

std::string Presenter::banner(std::string_view headword, std::string_view title) const {
  const Theme& theme = *theme_;

  if (theme.colorful() && color_ != ColorMode::none) {
    const Style bar = theme[Role::banner];
    std::string left = std::format(" {} ", headword);
    if (text::display_width(left) > width_) left = text::truncate(left, width_);
    const std::size_t left_width = text::display_width(left);

    std::string right = title.empty() ? std::string{} : std::format("{} ", title);
    if (left_width + text::display_width(right) > width_) {
      right = width_ > left_width + 2 ? text::truncate(title, width_ - left_width - 1) + " " : std::string{};
    }
    const std::size_t right_width = text::display_width(right);
    const std::size_t pad = width_ > left_width + right_width ? width_ - left_width - right_width : 0;

    std::string out = cli::paint(left, bar.layered(theme[Role::headword]), color_);
    out += cli::paint(std::string(pad, ' '), bar, color_);
    if (!right.empty()) out += cli::paint(right, bar.layered(theme[Role::dictionary_title]), color_);
    return out;
  }

  // No background colours available: headword, right-aligned title, a rule.
  std::string line = paint(headword, Role::headword);
  const std::size_t used = text::display_width(headword);
  if (!title.empty()) {
    const std::size_t title_width = text::display_width(title);
    if (used + 3 + title_width <= width_) {
      line += std::string(width_ - used - title_width, ' ');
      line += paint(title, Role::dictionary_title);
    } else if (width_ > used + 4) {
      line += "  ";
      line += paint(text::truncate(title, width_ - used - 2), Role::dictionary_title);
    }
  }
  return line + "\n" + rule();
}

std::string Presenter::entry(const Dictionary& dictionary, const Entry& entry, const HtmlRenderer& shared) const {
  // The dictionary's stylesheet decides which custom tags are blocks, hidden
  // or coloured, so each dictionary renders with its own sheet attached.
  RenderOptions options = shared.options();
  options.stylesheets.clear();
  if (css_enabled_) {
    if (const css::Sheet* sheet = dictionary.stylesheet()) options.stylesheets.push_back(sheet);
  }
  const HtmlRenderer renderer{shared.theme(), options};

  std::string out = banner(entry.headword, dictionary.metadata().title);
  out += '\n';
  const std::string indent(renderer.options().indent, ' ');
  if (!entry.via.empty()) {
    out += indent;
    out += paint(std::format("↳ via {}", join(entry.via, " → ", [](const std::string& s) { return s; })),
                 Role::muted);
    out += '\n';
  }
  std::string body = renderer.render(entry.definition);
  if (text::trim(text::strip_ansi(body)).empty()) body = indent + paint("(empty entry)", Role::muted);
  out += body;
  out += '\n';
  return out;
}

std::string Presenter::entries(const Shelf::LookupOutcome& outcome, const HtmlRenderer& renderer) const {
  std::string out;
  bool first = true;
  for (const Shelf::Hit& hit : outcome.hits) {
    for (const Entry& e : hit.entries) {
      if (!std::exchange(first, false)) out += '\n';
      out += entry(*hit.dictionary, e, renderer);
    }
  }
  return out;
}

std::string Presenter::not_found(std::string_view word, std::span<const Shelf::Candidate> suggestions,
                                 std::span<const Shelf::Match> prefixes) const {
  std::string out = paint("✗", Role::error);
  out += ' ';
  out += paint("no entry for", Role::muted);
  out += ' ';
  out += paint(std::format("“{}”", word), Role::strong);
  out += '\n';
  if (!suggestions.empty()) {
    out += "  ";
    out += paint("did you mean:", Role::muted);
    out += ' ';
    out += join(suggestions, paint(", ", Role::muted),
                [this](const Shelf::Candidate& c) { return paint(c.suggestion.key, Role::key); });
    out += '\n';
  } else if (!prefixes.empty()) {
    out += "  ";
    out += paint("headwords starting the same way:", Role::muted);
    out += ' ';
    out += join(prefixes, paint(", ", Role::muted),
                [this](const Shelf::Match& m) { return paint(m.key.key, Role::key); });
    out += '\n';
  }
  return out;
}

std::string Presenter::highlight(std::string_view key, std::string_view pattern) const {
  const std::string_view needle = text::trim(pattern);
  if (needle.empty()) return std::string(key);
  const std::string lower_key = text::to_lower_ascii(key);
  const std::string lower_needle = text::to_lower_ascii(needle);
  const std::size_t at = lower_key.find(lower_needle);
  if (at == std::string::npos) return std::string(key);
  std::string out(key.substr(0, at));
  out += paint(key.substr(at, needle.size()), Role::highlight);
  out += key.substr(at + needle.size());
  return out;
}

std::string Presenter::matches(std::string_view pattern, std::span<const Shelf::Match> found, bool numbered,
                               bool show_dictionary) const {
  std::string out;
  std::size_t index = 0;
  for (const Shelf::Match& match : found) {
    ++index;
    out += numbered ? paint(std::format("{:>4}  ", index), Role::muted) : std::string("  ");
    out += highlight(match.key.key, pattern);
    if (show_dictionary) {
      out += "  ";
      out += paint(std::format("· {}", match.dictionary->metadata().title), Role::muted);
    }
    out += '\n';
  }
  return out;
}

std::string Presenter::info(const Dictionary& dictionary) const {
  const Metadata& meta = dictionary.metadata();
  const Header& header = meta.header;
  const ArchiveStats& stats = dictionary.archive().stats();
  std::string out = banner(meta.title, std::format("{} · {} entries", to_string(meta.kind),
                                                   text::with_thousands(meta.entries)));
  out += '\n';

  constexpr std::size_t label_width = 13;
  const auto row = [&](std::string_view label, std::string_view value) {
    if (value.empty()) return;
    out += "  ";
    out += paint(text::pad_right(label, label_width), Role::key);
    out += paint(value, Role::value);
    out += '\n';
  };

  row("File", std::format("{} ({})", meta.path.string(), text::human_size(meta.file_size)));
  row("Engine", std::format("{} (requires {})", header.version.str(), header.required_version.str()));
  row("Encoding", header.encoding.name);
  row("Format", header.format);
  row("Created", header.creation_date);
  row("Keys", std::format("{}, punctuation {}", header.case_sensitive ? "case-sensitive" : "case-insensitive",
                          header.strip_key ? "ignored" : "significant"));
  std::string blocks = std::format("{} key blocks", text::with_thousands(stats.key_blocks));
  for (std::size_t i = 0; i < stats.key_blocks_by_compression.size(); ++i) {
    if (stats.key_blocks != 0 && stats.key_blocks_by_compression[i] == stats.key_blocks) {
      blocks += std::format(" ({})", to_string(static_cast<Compression>(i)));
    }
  }
  row("Blocks", std::format("{}, {} record blocks ({} stored, {} unpacked)", blocks,
                            text::with_thousands(stats.record_blocks), text::human_size(stats.compressed_record_bytes),
                            text::human_size(stats.record_bytes)));
  if (header.encryption) row("Encrypted", header.encryption.key_index ? "key index (2)" : "key blocks (1)");
  if (!header.stylesheet.empty()) row("StyleSheet", std::format("{} rules", header.stylesheet.rules.size()));
  if (const auto& files = dictionary.stylesheet_files(); !files.empty()) {
    std::string names;
    for (const auto& file : files) names += (names.empty() ? "" : ", ") + file.filename().string();
    const css::Sheet* sheet = dictionary.stylesheet();
    row("CSS", std::format("{} ({} rules{})", names, sheet ? sheet->size() : 0, css_enabled_ ? "" : ", ignored"));
  }
  for (const std::string& warning : dictionary.archive().warnings()) row("Note", warning);

  if (!text::trim(header.description).empty()) {
    out += "  ";
    out += paint("Description", Role::key);
    out += '\n';
    const std::string description = renderer(4, false).render(header.description);
    if (!text::trim(text::strip_ansi(description)).empty()) {
      out += description;
      out += '\n';
    }
  }
  return out;
}

std::string Presenter::resource_summary(const Dictionary& dictionary, std::string_view name,
                                        std::size_t bytes) const {
  std::string out = banner(name, dictionary.metadata().title);
  out += '\n';
  out += "  ";
  out += paint(std::format("{} · {}", mime_type_for(name), text::human_size(bytes)), Role::label);
  out += '\n';
  out += "  ";
  out += paint(std::format("extract it with: minedict extract '{}' -o <file>", name), Role::muted);
  out += '\n';
  return out;
}

std::string Presenter::problem(std::string_view message) const {
  return paint("✗ ", Role::error) + std::string(message);
}

std::string Presenter::warning(std::string_view message) const {
  return paint("! ", Role::warning) + std::string(message);
}

std::string Presenter::note(std::string_view message) const { return paint(message, Role::muted); }

}  // namespace minedict::cli
