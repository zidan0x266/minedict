// minedict - BSD 3-Clause License; see README.
//
// Just enough CSS to render a dictionary: which custom elements are blocks,
// which are hidden, what colour and weight they carry, and the labels that
// `::before` / `::after` rules insert. Layout properties are ignored.
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "minedict/cli/html.hpp"

namespace minedict::cli::css {

enum class Display { inline_, inline_block, block, none };
enum class Pseudo { none, before, after };

/// One piece of a `content:` value: literal text, or a `counter(name)`.
struct ContentPart {
  std::string text;
  std::string counter;  // non-empty: substitute the counter's value
};

struct CounterAction {
  std::string name;
  int value{0};  // reset target, or increment delta
};

/// The computed subset of properties for one element.
struct Declarations {
  std::optional<Display> display;
  std::optional<bool> hidden;  // visibility
  std::optional<std::string> color;
  std::optional<bool> bold;
  std::optional<bool> italic;
  std::optional<bool> underline;
  std::optional<bool> strike;
  std::optional<bool> preformatted;  // white-space: pre*
  std::optional<bool> space_before;  // a left margin or padding
  std::optional<bool> space_after;   // a right margin or padding
  std::optional<bool> blank_before;  // a top margin or padding of at least half a line
  std::optional<bool> blank_after;   // a bottom margin or padding of at least half a line
  std::optional<std::vector<ContentPart>> content;  // a ::before / ::after rule
  std::vector<CounterAction> counter_reset;
  std::vector<CounterAction> counter_increment;

  /// Overlay `over` onto this (later rules win).
  void merge(const Declarations& over);
  [[nodiscard]] bool empty() const noexcept;
};

/// One element on the path from the document root to the element styled.
struct Element {
  std::string_view tag;
  std::vector<std::string_view> classes;
  std::string_view id;
  const std::vector<html::Attribute>* attributes{nullptr};
};

class Sheet {
 public:
  [[nodiscard]] static Sheet parse(std::string_view source);

  [[nodiscard]] bool empty() const noexcept { return rules_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return rules_.size(); }

  /// Cascade every rule matching the last element of `path`.
  [[nodiscard]] Declarations resolve(std::span<const Element> path, Pseudo pseudo = Pseudo::none) const;

 private:
  struct AttributeTest {
    std::string name;
    std::optional<std::string> value;
  };
  struct Compound {
    std::string tag;  // empty: any element
    std::vector<std::string> classes;
    std::string id;
    std::vector<AttributeTest> attributes;
    [[nodiscard]] bool matches(const Element& element) const;
  };
  struct Rule {
    std::vector<Compound> chain;  // ancestors first, subject last
    Pseudo pseudo{Pseudo::none};
    int specificity{0};
    Declarations declarations;
    [[nodiscard]] bool matches(std::span<const Element> path) const;
  };

  std::vector<Rule> rules_;  // sorted by specificity, then source order
};

/// Split a `class` attribute value into class names.
[[nodiscard]] std::vector<std::string_view> class_names(std::string_view attribute);

/// Decode CSS escapes such as `\021D2 ` or `\:`.
[[nodiscard]] std::string unescape(std::string_view s);

}  // namespace minedict::cli::css
