// minedict - BSD 3-Clause License; see README.
//
// A forgiving HTML tokenizer: dictionary markup is rarely well-formed, so
// this never fails, it just does its best with whatever it is given.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace minedict::cli::html {

struct Attribute {
  std::string name;   // lower-case
  std::string value;  // entity-decoded
};

struct Tag {
  std::string name;  // lower-case
  std::vector<Attribute> attributes;
  bool closing{false};
  bool self_closing{false};

  [[nodiscard]] std::optional<std::string_view> attribute(std::string_view name) const noexcept;
};

struct Text {
  std::string content;  // raw: entities are still encoded
};

struct Comment {
  std::string content;
};

using Token = std::variant<Text, Tag, Comment>;

[[nodiscard]] std::vector<Token> tokenize(std::string_view html);

/// Elements that never have content (`<br>`, `<img>`, ...).
[[nodiscard]] bool is_void_element(std::string_view name) noexcept;

/// Elements that start on their own line.
[[nodiscard]] bool is_block_element(std::string_view name) noexcept;

/// Elements whose content is raw text (`<script>`, `<style>`).
[[nodiscard]] bool is_raw_text_element(std::string_view name) noexcept;

}  // namespace minedict::cli::html
