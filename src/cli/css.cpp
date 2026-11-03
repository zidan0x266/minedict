// minedict - BSD 3-Clause License; see README.
#include "minedict/cli/css.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <ranges>

#include "minedict/cli/text.hpp"
#include "minedict/unicode.hpp"

namespace minedict::cli::css {
namespace {

std::string strip_comments(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    if (s.substr(i, 2) == "/*") {
      const std::size_t end = s.find("*/", i + 2);
      i = end == std::string_view::npos ? s.size() : end + 2;
      continue;
    }
    out += s[i++];
  }
  return out;
}

/// Index just past the '}' that closes the block opened at `open`.
std::size_t skip_block(std::string_view s, std::size_t open) {
  int depth = 0;
  for (std::size_t i = open; i < s.size(); ++i) {
    if (s[i] == '{') ++depth;
    if (s[i] == '}' && --depth == 0) return i + 1;
  }
  return s.size();
}

std::optional<double> leading_number(std::string_view value) {
  double number = 0;
  const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), number);
  if (ec != std::errc{}) return std::nullopt;
  return number;
}

bool nonzero_length(std::string_view value) {
  const auto number = leading_number(text::trim(value));
  return number && *number > 0.0;
}

/// Box shorthand: 1-4 values -> {top, right, bottom, left}.
std::array<std::string_view, 4> box_sides(std::string_view value) {
  std::vector<std::string_view> parts;
  for (const auto part : std::views::split(value, ' ')) {
    const std::string_view item = text::trim(std::string_view{part.begin(), part.end()});
    if (!item.empty()) parts.push_back(item);
  }
  switch (parts.size()) {
    case 1: return {parts[0], parts[0], parts[0], parts[0]};
    case 2: return {parts[0], parts[1], parts[0], parts[1]};
    case 3: return {parts[0], parts[1], parts[2], parts[1]};
    case 4: return {parts[0], parts[1], parts[2], parts[3]};
    default: return {"0", "0", "0", "0"};
  }
}

/// The pieces of a `content:` value: string literals and counter() references.
/// Anything else (attr(), url(), open-quote, ...) is dropped.
std::vector<ContentPart> parse_content(std::string_view value) {
  std::vector<ContentPart> parts;
  for (std::size_t i = 0; i < value.size();) {
    const char c = value[i];
    if (c == '"' || c == '\'') {
      std::size_t j = i + 1;
      while (j < value.size() && value[j] != c) {
        if (value[j] == '\\' && j + 1 < value.size()) ++j;
        ++j;
      }
      std::string literal = unescape(value.substr(i + 1, j - i - 1));
      if (!literal.empty()) parts.push_back(ContentPart{.text = std::move(literal), .counter = {}});
      i = j + 1;
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) != 0) {
      std::size_t j = i;
      while (j < value.size() && (std::isalnum(static_cast<unsigned char>(value[j])) != 0 || value[j] == '-')) ++j;
      const std::string word = text::to_lower_ascii(value.substr(i, j - i));
      if ((word == "counter" || word == "counters") && j < value.size() && value[j] == '(') {
        const std::size_t close = value.find(')', j);
        const std::string_view inside = value.substr(j + 1, (close == std::string_view::npos ? value.size() : close) - j - 1);
        const std::string_view name = text::trim(inside.substr(0, inside.find(',')));
        parts.push_back(ContentPart{.text = {}, .counter = std::string(name)});
        i = close == std::string_view::npos ? value.size() : close + 1;
        continue;
      }
      i = j;
      continue;
    }
    ++i;
  }
  return parts;
}

/// `counter-reset` / `counter-increment` values: `name [integer] ...`.
std::vector<CounterAction> parse_counter_actions(std::string_view value, int default_value) {
  std::vector<CounterAction> actions;
  std::vector<std::string_view> tokens;
  for (const auto part : std::views::split(value, ' ')) {
    const std::string_view token = text::trim(std::string_view{part.begin(), part.end()});
    if (!token.empty()) tokens.push_back(token);
  }
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i] == "none") continue;
    CounterAction action{.name = std::string(tokens[i]), .value = default_value};
    if (i + 1 < tokens.size()) {
      int number = 0;
      const auto [ptr, ec] = std::from_chars(tokens[i + 1].data(), tokens[i + 1].data() + tokens[i + 1].size(), number);
      if (ec == std::errc{} && ptr == tokens[i + 1].data() + tokens[i + 1].size()) {
        action.value = number;
        ++i;
      }
    }
    actions.push_back(std::move(action));
  }
  return actions;
}

/// A vertical margin or padding of at least half a line: 0.5em, 8px, 6pt...
bool blank_worthy_length(std::string_view value) {
  const auto number = leading_number(value);
  if (!number || *number <= 0) return false;
  if (value.contains("em") || value.contains("ex") || value.contains("ch")) return *number >= 0.5;
  if (value.contains("pt")) return *number >= 6;
  if (value.contains("%")) return *number >= 50;
  return *number >= 8;  // px, or a bare number
}

void apply_declaration(Declarations& d, std::string_view name, std::string_view raw_value) {
  std::string value = text::to_lower_ascii(text::trim(raw_value));
  if (const std::size_t bang = value.find("!important"); bang != std::string::npos) {
    value = std::string(text::trim(std::string_view{value}.substr(0, bang)));
  }
  if (name == "display") {
    if (value == "none") {
      d.display = Display::none;
    } else if (value.starts_with("inline-")) {
      d.display = Display::inline_block;  // a box: the renderer treats its edges as word boundaries
    } else if (value.starts_with("inline")) {
      d.display = Display::inline_;
    } else if (value == "table-cell") {
      d.display = Display::inline_;  // cells sit side by side
    } else if (value == "block" || value == "flex" || value == "grid" || value == "table" || value == "table-row" ||
               value == "list-item" || value == "table-caption" || value == "flow-root") {
      d.display = Display::block;
    }
  } else if (name == "visibility") {
    if (value == "hidden" || value == "collapse") d.hidden = true;
    else if (value == "visible") d.hidden = false;
  } else if (name == "color") {
    d.color = value;
  } else if (name == "font-weight") {
    const auto number = leading_number(value);
    if (value == "bold" || value == "bolder" || (number && *number >= 600)) d.bold = true;
    else if (value == "normal" || value == "lighter" || (number && *number < 600)) d.bold = false;
  } else if (name == "font-style") {
    if (value == "italic" || value == "oblique") d.italic = true;
    else if (value == "normal") d.italic = false;
  } else if (name == "font") {
    if (value.contains("italic")) d.italic = true;
    if (value.contains("bold")) d.bold = true;
  } else if (name == "text-decoration" || name == "text-decoration-line") {
    if (value == "none") {
      d.underline = false;
      d.strike = false;
    } else {
      if (value.contains("underline")) d.underline = true;
      if (value.contains("line-through")) d.strike = true;
    }
  } else if (name == "white-space") {
    d.preformatted = value.starts_with("pre");
  } else if (name == "margin-top" || name == "padding-top") {
    if (blank_worthy_length(value)) d.blank_before = true;
  } else if (name == "margin-bottom" || name == "padding-bottom") {
    if (blank_worthy_length(value)) d.blank_after = true;
  } else if (name == "margin-left" || name == "padding-left") {
    if (nonzero_length(value)) d.space_before = true;
  } else if (name == "margin-right" || name == "padding-right") {
    if (nonzero_length(value)) d.space_after = true;
  } else if (name == "margin" || name == "padding") {
    const auto sides = box_sides(value);
    if (nonzero_length(sides[3])) d.space_before = true;
    if (nonzero_length(sides[1])) d.space_after = true;
    if (blank_worthy_length(sides[0])) d.blank_before = true;
    if (blank_worthy_length(sides[2])) d.blank_after = true;
  } else if (name == "content") {
    d.content = (value == "none" || value == "normal") ? std::vector<ContentPart>{} : parse_content(raw_value);
  } else if (name == "counter-reset") {
    d.counter_reset = parse_counter_actions(value, 0);
  } else if (name == "counter-increment") {
    d.counter_increment = parse_counter_actions(value, 1);
  }
}

Declarations parse_declarations(std::string_view body) {
  Declarations d;
  for (const auto part : std::views::split(body, ';')) {
    const std::string_view declaration{part.begin(), part.end()};
    const std::size_t colon = declaration.find(':');
    if (colon == std::string_view::npos) continue;
    const std::string name = text::to_lower_ascii(text::trim(declaration.substr(0, colon)));
    apply_declaration(d, name, declaration.substr(colon + 1));
  }
  return d;
}

/// Position of an unescaped character at the top level (outside brackets).
std::size_t find_top_level(std::string_view s, char wanted) {
  int depth = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\') {
      ++i;
      continue;
    }
    if (s[i] == '[') ++depth;
    else if (s[i] == ']') --depth;
    else if (depth == 0 && s[i] == wanted) return i;
  }
  return std::string_view::npos;
}

}  // namespace

// ---------------------------------------------------------------------------

std::string unescape(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size();) {
    if (s[i] != '\\' || i + 1 >= s.size()) {
      out += s[i++];
      continue;
    }
    ++i;
    std::size_t j = i;
    while (j < s.size() && j - i < 6 && std::isxdigit(static_cast<unsigned char>(s[j])) != 0) ++j;
    if (j > i) {
      std::uint32_t code_point = 0;
      std::from_chars(s.data() + i, s.data() + j, code_point, 16);
      if (code_point != 0 && code_point <= 0x10FFFF) unicode::append_utf8(out, static_cast<char32_t>(code_point));
      if (j < s.size() && s[j] == ' ') ++j;  // a space terminates the escape
      i = j;
    } else {
      out += s[i++];  // escaped punctuation such as \:
    }
  }
  return out;
}

std::vector<std::string_view> class_names(std::string_view attribute) {
  std::vector<std::string_view> names;
  for (const auto part : std::views::split(attribute, ' ')) {
    const std::string_view name = text::trim(std::string_view{part.begin(), part.end()});
    if (!name.empty()) names.push_back(name);
  }
  return names;
}

void Declarations::merge(const Declarations& over) {
  if (over.display) display = over.display;
  if (over.hidden) hidden = over.hidden;
  if (over.color) color = over.color;
  if (over.bold) bold = over.bold;
  if (over.italic) italic = over.italic;
  if (over.underline) underline = over.underline;
  if (over.strike) strike = over.strike;
  if (over.preformatted) preformatted = over.preformatted;
  if (over.space_before) space_before = over.space_before;
  if (over.space_after) space_after = over.space_after;
  if (over.blank_before) blank_before = over.blank_before;
  if (over.blank_after) blank_after = over.blank_after;
  if (over.content) content = over.content;
  if (!over.counter_reset.empty()) counter_reset = over.counter_reset;
  if (!over.counter_increment.empty()) counter_increment = over.counter_increment;
}

bool Declarations::empty() const noexcept {
  return !display && !hidden && !color && !bold && !italic && !underline && !strike && !preformatted &&
         !space_before && !space_after && !blank_before && !blank_after && !content && counter_reset.empty() && counter_increment.empty();
}

bool Sheet::Compound::matches(const Element& element) const {
  if (!tag.empty() && !text::iequals_ascii(tag, element.tag)) return false;
  if (!id.empty() && id != element.id) return false;
  for (const std::string& wanted : classes) {
    if (!std::ranges::contains(element.classes, std::string_view{wanted})) return false;
  }
  for (const AttributeTest& test : attributes) {
    if (element.attributes == nullptr) return false;
    const auto it = std::ranges::find_if(*element.attributes, [&](const html::Attribute& a) {
      return text::iequals_ascii(a.name, test.name) && (!test.value || a.value == *test.value);
    });
    if (it == element.attributes->end()) return false;
  }
  return true;
}

bool Sheet::Rule::matches(std::span<const Element> path) const {
  if (chain.empty() || path.empty()) return false;
  if (!chain.back().matches(path.back())) return false;
  // Remaining compounds must match ancestors, in order (descendant combinator).
  std::size_t ancestor = path.size() - 1;
  for (std::size_t i = chain.size() - 1; i-- > 0;) {
    bool found = false;
    while (ancestor-- > 0) {
      if (chain[i].matches(path[ancestor])) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

Sheet Sheet::parse(std::string_view source) {
  Sheet sheet;
  const std::string css = strip_comments(source);
  const std::string_view s{css};

  std::size_t pos = 0;
  while (pos < s.size()) {
    while (pos < s.size() && (std::isspace(static_cast<unsigned char>(s[pos])) != 0 || s[pos] == ';')) ++pos;
    if (pos >= s.size()) break;

    if (s[pos] == '@') {  // @font-face, @media, @import, ...: skip
      const std::size_t semicolon = s.find(';', pos);
      const std::size_t brace = s.find('{', pos);
      if (brace == std::string_view::npos || (semicolon != std::string_view::npos && semicolon < brace)) {
        pos = semicolon == std::string_view::npos ? s.size() : semicolon + 1;
      } else {
        pos = skip_block(s, brace);
      }
      continue;
    }

    const std::size_t open = s.find('{', pos);
    if (open == std::string_view::npos) break;
    const std::size_t close = s.find('}', open);
    const std::string_view selectors = s.substr(pos, open - pos);
    const Declarations declarations =
        parse_declarations(s.substr(open + 1, (close == std::string_view::npos ? s.size() : close) - open - 1));
    pos = close == std::string_view::npos ? s.size() : close + 1;
    if (declarations.empty()) continue;

    for (const auto part : std::views::split(selectors, ',')) {
      std::string_view selector = text::trim(std::string_view{part.begin(), part.end()});
      if (selector.empty()) continue;

      Rule rule;
      rule.declarations = declarations;

      // Pseudo-elements we understand; any other pseudo makes the rule inert.
      for (const auto [suffix, pseudo] : {std::pair{std::string_view{"::before"}, Pseudo::before},
                                          std::pair{std::string_view{":before"}, Pseudo::before},
                                          std::pair{std::string_view{"::after"}, Pseudo::after},
                                          std::pair{std::string_view{":after"}, Pseudo::after}}) {
        if (selector.ends_with(suffix)) {
          rule.pseudo = pseudo;
          selector = text::trim(selector.substr(0, selector.size() - suffix.size()));
          break;
        }
      }
      if (find_top_level(selector, ':') != std::string_view::npos) continue;  // :hover, :not(...), ...
      if (find_top_level(selector, '+') != std::string_view::npos ||
          find_top_level(selector, '~') != std::string_view::npos) {
        continue;  // sibling combinators
      }

      bool supported = true;
      std::string compound_text;
      const auto flush_compound = [&] {
        const std::string_view token = text::trim(compound_text);
        if (token.empty()) return;
        Compound compound;
        std::size_t i = 0;
        // type selector
        std::size_t start = i;
        while (i < token.size() && token[i] != '.' && token[i] != '#' && token[i] != '[') {
          if (token[i] == '\\') ++i;
          ++i;
        }
        const std::string type = text::to_lower_ascii(unescape(token.substr(start, i - start)));
        if (type != "*") compound.tag = type;
        while (i < token.size()) {
          const char kind = token[i++];
          start = i;
          if (kind == '[') {
            while (i < token.size() && token[i] != ']') ++i;
            const std::string_view inside = token.substr(start, i - start);
            if (i < token.size()) ++i;
            AttributeTest test;
            const std::size_t eq = inside.find('=');
            if (eq == std::string_view::npos) {
              test.name = text::to_lower_ascii(text::trim(inside));
            } else {
              std::string_view name = text::trim(inside.substr(0, eq));
              if (!name.empty() && (name.back() == '~' || name.back() == '|' || name.back() == '^' ||
                                    name.back() == '$' || name.back() == '*')) {
                supported = false;  // partial-match attribute selectors
              }
              std::string_view value = text::trim(inside.substr(eq + 1));
              if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'')) {
                value = value.substr(1, value.size() - 2);
              }
              test.name = text::to_lower_ascii(name);
              test.value = unescape(value);
            }
            compound.attributes.push_back(std::move(test));
            continue;
          }
          while (i < token.size() && token[i] != '.' && token[i] != '#' && token[i] != '[') {
            if (token[i] == '\\') ++i;
            ++i;
          }
          const std::string name = unescape(token.substr(start, i - start));
          if (kind == '.') compound.classes.push_back(name);
          else compound.id = name;
        }
        rule.specificity += 100 * static_cast<int>(!compound.id.empty()) +
                            10 * static_cast<int>(compound.classes.size() + compound.attributes.size()) +
                            static_cast<int>(!compound.tag.empty());
        rule.chain.push_back(std::move(compound));
        compound_text.clear();
      };

      int bracket_depth = 0;
      for (std::size_t i = 0; i < selector.size(); ++i) {
        const char c = selector[i];
        if (c == '\\' && i + 1 < selector.size()) {
          compound_text += c;
          compound_text += selector[++i];
          continue;
        }
        if (c == '[') ++bracket_depth;
        if (c == ']') --bracket_depth;
        if (bracket_depth == 0 && (std::isspace(static_cast<unsigned char>(c)) != 0 || c == '>')) {
          flush_compound();  // descendant and child combinators both mean "inside"
          continue;
        }
        compound_text += c;
      }
      flush_compound();
      if (rule.pseudo != Pseudo::none) rule.specificity += 1;
      if (supported && !rule.chain.empty()) sheet.rules_.push_back(std::move(rule));
    }
  }
  std::ranges::stable_sort(sheet.rules_, {}, &Rule::specificity);
  return sheet;
}

Declarations Sheet::resolve(std::span<const Element> path, Pseudo pseudo) const {
  Declarations result;
  for (const Rule& rule : rules_) {
    if (rule.pseudo != pseudo) continue;
    if (rule.matches(path)) result.merge(rule.declarations);
  }
  return result;
}

}  // namespace minedict::cli::css
