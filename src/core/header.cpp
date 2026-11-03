#include "minedict/header.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <ranges>
#include <vector>

#include "minedict/unicode.hpp"

namespace minedict {
namespace {

std::string_view trim(std::string_view text) noexcept {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                           text.front() == '\n' || text.front() == '\0')) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n' || text.back() == '\0')) {
    text.remove_suffix(1);
  }
  return text;
}

std::string lower_ascii(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
  }
  return out;
}

bool flag(const std::map<std::string, std::string>& attributes, std::string_view name, bool fallback) {
  const auto it = attributes.find(std::string(name));
  if (it == attributes.end()) return fallback;
  const std::string value = lower_ascii(trim(it->second));
  if (value == "yes" || value == "true" || value == "1") return true;
  if (value == "no" || value == "false" || value == "0") return false;
  return fallback;
}

Encryption parse_encryption(std::string_view value) {
  const std::string v = lower_ascii(trim(value));
  if (v.empty() || v == "no" || v == "false") return {};
  if (v == "yes" || v == "true") return {.key_blocks = true};
  unsigned bits = 0;
  std::from_chars(v.data(), v.data() + v.size(), bits);
  return {.key_blocks = (bits & 1U) != 0, .key_index = (bits & 2U) != 0};
}

}  // namespace

std::optional<Version> Version::parse(std::string_view text) noexcept {
  text = trim(text);
  Version v{};
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  auto [p, ec] = std::from_chars(begin, end, v.major);
  if (ec != std::errc{}) return std::nullopt;
  if (p < end && *p == '.') std::from_chars(p + 1, end, v.minor);
  return v;
}

std::string Version::str() const { return std::format("{}.{}", major, minor); }

StyleSheet StyleSheet::parse(std::string_view raw) {
  StyleSheet sheet;
  if (trim(raw).empty()) return sheet;
  std::vector<std::string> lines;
  for (const auto part : std::views::split(raw, '\n')) {
    std::string_view line{part.begin(), part.end()};
    if (line.ends_with('\r')) line.remove_suffix(1);
    lines.emplace_back(line);
  }
  // Groups of three lines: number, begin snippet, end snippet.
  for (std::size_t i = 0; i + 1 < lines.size(); i += 3) {
    const std::string number{trim(lines[i])};
    if (number.empty()) continue;
    sheet.rules[number] = Rule{.begin = lines[i + 1], .end = i + 2 < lines.size() ? lines[i + 2] : std::string{}};
  }
  return sheet;
}

std::string StyleSheet::apply(std::string_view definition) const {
  if (rules.empty()) return std::string(definition);
  std::string out;
  out.reserve(definition.size() + 64);
  const Rule* open = nullptr;  // rule whose `end` is still pending

  std::size_t i = 0;
  while (i < definition.size()) {
    if (definition[i] == '`') {
      const std::size_t close = definition.find('`', i + 1);
      const bool digits = close != std::string_view::npos && close > i + 1 &&
                          std::ranges::all_of(definition.substr(i + 1, close - i - 1),
                                              [](char c) { return c >= '0' && c <= '9'; });
      if (digits) {
        if (open != nullptr) out += open->end;
        open = nullptr;
        if (const auto rule = rules.find(std::string(definition.substr(i + 1, close - i - 1))); rule != rules.end()) {
          out += rule->second.begin;
          open = &rule->second;
        }
        i = close + 1;
        continue;
      }
    }
    out += definition[i++];
  }
  if (open != nullptr) out += open->end;
  return out;
}

std::string Header::attribute(std::string_view name) const {
  const auto it = attributes.find(std::string(name));
  return it == attributes.end() ? std::string{} : it->second;
}

std::string decode_xml_entities(std::string_view text) {
  struct Named {
    std::string_view name;
    char32_t code_point;
  };
  constexpr std::array named{Named{"lt", '<'},    Named{"gt", '>'},   Named{"amp", '&'},   Named{"quot", '"'},
                             Named{"apos", '\''}, Named{"nbsp", 0xA0}};
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] != '&') {
      out += text[i++];
      continue;
    }
    const std::size_t semicolon = text.find(';', i + 1);
    if (semicolon == std::string_view::npos || semicolon - i > 12) {
      out += text[i++];
      continue;
    }
    const std::string_view body = text.substr(i + 1, semicolon - i - 1);
    std::optional<char32_t> cp;
    if (body.starts_with('#')) {
      unsigned value = 0;
      const bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
      const std::string_view digits = body.substr(hex ? 2 : 1);
      const auto [p, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value, hex ? 16 : 10);
      if (ec == std::errc{} && p == digits.data() + digits.size() && value != 0 && value <= 0x10FFFF) cp = value;
    } else if (const auto it = std::ranges::find(named, body, &Named::name); it != named.end()) {
      cp = it->code_point;
    }
    if (!cp) {
      out += text[i++];
      continue;
    }
    unicode::append_utf8(out, *cp);
    i = semicolon + 1;
  }
  return out;
}

Result<std::map<std::string, std::string>> parse_attributes(std::string_view xml) {
  std::map<std::string, std::string> attributes;
  const std::size_t open = xml.find('<');
  if (open == std::string_view::npos) return fail(ErrorKind::format, "header has no XML tag");
  std::size_t pos = open + 1;
  const auto at_end = [&] { return pos >= xml.size() || xml[pos] == '>' || xml.substr(pos, 2) == "/>"; };
  const auto skip_space = [&] {
    while (pos < xml.size() && (xml[pos] == ' ' || xml[pos] == '\t' || xml[pos] == '\r' || xml[pos] == '\n')) ++pos;
  };
  // Tag name.
  while (pos < xml.size() && xml[pos] != ' ' && xml[pos] != '\t' && xml[pos] != '\r' && xml[pos] != '\n' && !at_end()) {
    ++pos;
  }
  for (;;) {
    skip_space();
    if (at_end()) break;
    const std::size_t name_start = pos;
    while (pos < xml.size() && xml[pos] != '=' && xml[pos] != ' ' && xml[pos] != '\t' && xml[pos] != '\r' &&
           xml[pos] != '\n' && !at_end()) {
      ++pos;
    }
    const std::string name{xml.substr(name_start, pos - name_start)};
    skip_space();
    if (pos >= xml.size() || xml[pos] != '=') {
      if (name.empty()) break;  // stray character; stop rather than loop forever
      attributes[name] = "";    // bare attribute
      continue;
    }
    ++pos;  // '='
    skip_space();
    if (pos >= xml.size()) break;
    std::string value;
    if (xml[pos] == '"' || xml[pos] == '\'') {
      const char quote = xml[pos++];
      const std::size_t close = xml.find(quote, pos);
      if (close == std::string_view::npos) return fail(ErrorKind::format, "unterminated value for attribute '{}'", name);
      value = std::string(xml.substr(pos, close - pos));
      pos = close + 1;
    } else {
      const std::size_t value_start = pos;
      while (pos < xml.size() && xml[pos] != ' ' && xml[pos] != '\t' && xml[pos] != '\r' && xml[pos] != '\n' &&
             !at_end()) {
        ++pos;
      }
      value = std::string(xml.substr(value_start, pos - value_start));
    }
    if (!name.empty()) attributes[name] = decode_xml_entities(value);
  }
  if (attributes.empty()) return fail(ErrorKind::format, "header tag has no attributes");
  return attributes;
}

Result<Header> parse_header(ByteView utf16le_xml, Kind kind) {
  std::string xml = unicode::utf16le_to_utf8(utf16le_xml);
  if (xml.starts_with("\xEF\xBB\xBF")) xml.erase(0, 3);
  auto attributes = parse_attributes(xml);
  if (!attributes) return std::unexpected(attributes.error());

  Header header;
  header.attributes = std::move(*attributes);
  header.kind = kind;
  header.version = Version::parse(header.attribute("GeneratedByEngineVersion")).value_or(Version{1, 0});
  header.required_version = Version::parse(header.attribute("RequiredEngineVersion")).value_or(header.version);
  if (header.version >= Version{3, 0}) {
    return fail(ErrorKind::unsupported, "engine version {} files use a different layout", header.version.str());
  }
  // Resource files always store UTF-16LE keys, whatever the header says.
  header.encoding = kind == Kind::mdd ? Encoding::utf16le() : Encoding::from_name(header.attribute("Encoding"));
  header.encryption = parse_encryption(header.attribute("Encrypted"));
  header.case_sensitive = flag(header.attributes, "KeyCaseSensitive", false);
  header.strip_key = flag(header.attributes, "StripKey", true);
  header.title = std::string(trim(header.attribute("Title")));
  header.description = header.attribute("Description");
  header.format = std::string(trim(header.attribute("Format")));
  header.creation_date = std::string(trim(header.attribute("CreationDate")));
  header.register_by = std::string(trim(header.attribute("RegisterBy")));
  header.stylesheet = StyleSheet::parse(header.attribute("StyleSheet"));
  return header;
}

}  // namespace minedict
