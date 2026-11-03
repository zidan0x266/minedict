// minedict - the dictionary header: a UTF-16LE XML tag describing the file.
#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "minedict/bytes.hpp"
#include "minedict/encoding.hpp"

namespace minedict {

/// What a file holds: MDX = headwords and definitions, MDD = named resources.
enum class Kind : std::uint8_t { mdx, mdd };

[[nodiscard]] constexpr std::string_view to_string(Kind kind) noexcept { return kind == Kind::mdx ? "MDX" : "MDD"; }

/// Engine version such as 1.2 or 2.0; it decides integer widths and block layout.
struct Version {
  unsigned major{1};
  unsigned minor{0};

  [[nodiscard]] static std::optional<Version> parse(std::string_view text) noexcept;
  [[nodiscard]] std::string str() const;
  [[nodiscard]] constexpr auto operator<=>(const Version&) const noexcept = default;
};

/// The header's `Encrypted` flags.
struct Encryption {
  bool key_blocks{false};  // bit 1: needs the owner's registration code
  bool key_index{false};   // bit 2: the key-block index is scrambled with RIPEMD-128

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return key_blocks || key_index; }
};

/// The header's `StyleSheet`: numbered begin/end snippets that definitions
/// reference with `` `N` `` markers.
struct StyleSheet {
  struct Rule {
    std::string begin;
    std::string end;
    [[nodiscard]] bool operator==(const Rule&) const = default;
  };
  std::map<std::string, Rule> rules;

  [[nodiscard]] static StyleSheet parse(std::string_view raw);
  [[nodiscard]] bool empty() const noexcept { return rules.empty(); }

  /// Expand the `` `N` `` markers in a definition.
  [[nodiscard]] std::string apply(std::string_view definition) const;
};

struct Header {
  std::map<std::string, std::string> attributes;  // every attribute, entity-decoded
  Kind kind{Kind::mdx};
  Version version;           // GeneratedByEngineVersion
  Version required_version;  // RequiredEngineVersion
  Encoding encoding;
  Encryption encryption;
  bool case_sensitive{false};  // KeyCaseSensitive
  bool strip_key{true};        // StripKey
  std::string title;           // may contain HTML
  std::string description;     // HTML
  std::string format;          // "Html", "Text"
  std::string creation_date;
  std::string register_by;
  StyleSheet stylesheet;

  [[nodiscard]] constexpr bool is_v2() const noexcept { return version >= Version{2, 0}; }
  [[nodiscard]] constexpr NumberWidth number_width() const noexcept {
    return is_v2() ? NumberWidth::eight : NumberWidth::four;
  }
  /// An attribute by name, or empty.
  [[nodiscard]] std::string attribute(std::string_view name) const;
};

/// Replace `&lt;`, `&#x4e2d;` and friends with the characters they stand for.
[[nodiscard]] std::string decode_xml_entities(std::string_view text);

/// Attributes of the single tag a header consists of
/// (`<Dictionary GeneratedByEngineVersion="2.0" .../>`).
[[nodiscard]] Result<std::map<std::string, std::string>> parse_attributes(std::string_view xml);

/// Decode the UTF-16LE header text and interpret its attributes.
[[nodiscard]] Result<Header> parse_header(ByteView utf16le_xml, Kind kind);

}  // namespace minedict
