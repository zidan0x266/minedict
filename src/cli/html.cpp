// minedict - BSD 3-Clause License; see README.
#include "minedict/cli/html.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>

#include "minedict/cli/text.hpp"

namespace minedict::cli::html {
namespace {

constexpr std::array<std::string_view, 14> kVoidElements{
    "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr",
};

constexpr std::array<std::string_view, 46> kBlockElements{
    "address", "article", "aside",   "blockquote", "body",    "caption", "center",  "dd",      "details",
    "dialog",  "dir",     "div",     "dl",         "dt",      "fieldset", "figcaption", "figure", "footer",
    "form",    "h1",      "h2",      "h3",         "h4",      "h5",      "h6",      "head",    "header",
    "hgroup",  "hr",      "html",    "li",         "main",    "menu",    "nav",     "ol",      "p",
    "pre",     "section", "summary", "table",      "tbody",   "td",      "tfoot",   "th",      "thead",
    "tr",
};

constexpr bool is_name_char(char c) noexcept {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == ':' || c == '.';
}

constexpr bool is_blank(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/// Parse a tag starting at `pos` (which points at '<'). Returns the tag and
/// the position just past its '>' or nothing if this is not a tag at all.
std::optional<std::pair<Tag, std::size_t>> parse_tag(std::string_view s, std::size_t pos) {
  std::size_t i = pos + 1;
  Tag tag;
  if (i < s.size() && s[i] == '/') {
    tag.closing = true;
    ++i;
  }
  if (i >= s.size() || std::isalpha(static_cast<unsigned char>(s[i])) == 0) {
    return std::nullopt;
  }
  const std::size_t name_start = i;
  while (i < s.size() && is_name_char(s[i])) ++i;
  tag.name = text::to_lower_ascii(s.substr(name_start, i - name_start));

  while (i < s.size()) {
    while (i < s.size() && is_blank(s[i])) ++i;
    if (i >= s.size()) break;
    if (s[i] == '>') {
      return std::pair{std::move(tag), i + 1};
    }
    if (s[i] == '/') {
      ++i;
      if (i < s.size() && s[i] == '>') {
        tag.self_closing = true;
        return std::pair{std::move(tag), i + 1};
      }
      continue;
    }
    if (s[i] == '<') {
      // Unterminated tag; treat what we have as complete.
      return std::pair{std::move(tag), i};
    }
    // attribute name
    const std::size_t attr_start = i;
    while (i < s.size() && !is_blank(s[i]) && s[i] != '=' && s[i] != '>' && s[i] != '/' && s[i] != '<') ++i;
    Attribute attribute{.name = text::to_lower_ascii(s.substr(attr_start, i - attr_start)), .value = {}};
    while (i < s.size() && is_blank(s[i])) ++i;
    if (i < s.size() && s[i] == '=') {
      ++i;
      while (i < s.size() && is_blank(s[i])) ++i;
      if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
        const char quote = s[i++];
        const std::size_t value_start = i;
        while (i < s.size() && s[i] != quote) ++i;
        attribute.value = text::decode_entities(s.substr(value_start, i - value_start));
        if (i < s.size()) ++i;
      } else {
        const std::size_t value_start = i;
        while (i < s.size() && !is_blank(s[i]) && s[i] != '>') ++i;
        attribute.value = text::decode_entities(s.substr(value_start, i - value_start));
      }
    }
    if (!attribute.name.empty()) tag.attributes.push_back(std::move(attribute));
  }
  return std::pair{std::move(tag), s.size()};
}

std::size_t find_closing_tag(std::string_view s, std::size_t from, std::string_view name) {
  for (std::size_t i = from; i + name.size() + 2 <= s.size(); ++i) {
    if (s[i] == '<' && s[i + 1] == '/' && text::iequals_ascii(s.substr(i + 2, name.size()), name)) {
      return i;
    }
  }
  return std::string_view::npos;
}

}  // namespace

std::optional<std::string_view> Tag::attribute(std::string_view wanted) const noexcept {
  const auto it = std::ranges::find(attributes, wanted, &Attribute::name);
  if (it == attributes.end()) return std::nullopt;
  return std::string_view{it->value};
}

bool is_void_element(std::string_view name) noexcept { return std::ranges::contains(kVoidElements, name); }
bool is_block_element(std::string_view name) noexcept { return std::ranges::contains(kBlockElements, name); }
bool is_raw_text_element(std::string_view name) noexcept { return name == "script" || name == "style"; }

std::vector<Token> tokenize(std::string_view s) {
  std::vector<Token> tokens;
  std::size_t pos = 0;
  std::size_t text_start = 0;

  const auto flush_text = [&](std::size_t end) {
    if (end > text_start) tokens.emplace_back(Text{std::string(s.substr(text_start, end - text_start))});
  };

  while (pos < s.size()) {
    if (s[pos] != '<') {
      ++pos;
      continue;
    }
    if (s.substr(pos, 4) == "<!--") {
      flush_text(pos);
      const std::size_t end = s.find("-->", pos + 4);
      const std::size_t stop = end == std::string_view::npos ? s.size() : end;
      tokens.emplace_back(Comment{std::string(s.substr(pos + 4, stop - pos - 4))});
      pos = end == std::string_view::npos ? s.size() : end + 3;
      text_start = pos;
      continue;
    }
    if (pos + 1 < s.size() && (s[pos + 1] == '!' || s[pos + 1] == '?')) {
      flush_text(pos);
      const std::size_t end = s.find('>', pos);
      pos = end == std::string_view::npos ? s.size() : end + 1;
      text_start = pos;
      continue;
    }
    auto parsed = parse_tag(s, pos);
    if (!parsed) {
      ++pos;  // a literal '<'
      continue;
    }
    flush_text(pos);
    auto& [tag, next] = *parsed;
    pos = next;
    text_start = pos;
    const bool raw = !tag.closing && !tag.self_closing && is_raw_text_element(tag.name);
    const std::string raw_name = tag.name;
    tokens.emplace_back(std::move(tag));
    if (raw) {
      // Skip the raw content; emit the closing tag so the frame stack balances.
      const std::size_t close = find_closing_tag(s, pos, raw_name);
      if (close == std::string_view::npos) {
        pos = s.size();
      } else {
        const std::size_t end = s.find('>', close);
        pos = end == std::string_view::npos ? s.size() : end + 1;
        tokens.emplace_back(Tag{.name = raw_name, .attributes = {}, .closing = true, .self_closing = false});
      }
      text_start = pos;
    }
  }
  flush_text(s.size());
  return tokens;
}

}  // namespace minedict::cli::html
