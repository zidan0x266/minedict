// minedict - BSD 3-Clause License; see README.
//
// Turns a dictionary entry's HTML into styled, word-wrapped terminal text.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "minedict/cli/ansi.hpp"
#include "minedict/cli/css.hpp"
#include "minedict/cli/theme.hpp"

namespace minedict::cli {

struct RenderOptions {
  std::size_t width{80};              // total columns including `indent`
  std::size_t indent{0};              // leading columns on every line
  ColorMode color{ColorMode::none};
  bool show_images{true};             // render `<img>` as a placeholder
  bool show_sounds{true};             // render `sound://` links as a note glyph
  std::vector<const css::Sheet*> stylesheets;  // the dictionary's CSS, in cascade order
};

class HtmlRenderer {
 public:
  HtmlRenderer(const Theme& theme, RenderOptions options) noexcept
      : theme_(&theme), options_(options) {}

  [[nodiscard]] const RenderOptions& options() const noexcept { return options_; }
  [[nodiscard]] const Theme& theme() const noexcept { return *theme_; }

  /// Render `html` to lines joined by '\n' (no trailing newline).
  [[nodiscard]] std::string render(std::string_view html) const;

 private:
  const Theme* theme_;
  RenderOptions options_;
};

/// Convenience: unstyled, unwrapped plain text of an HTML fragment.
[[nodiscard]] std::string html_to_plain_text(std::string_view html);

}  // namespace minedict::cli
