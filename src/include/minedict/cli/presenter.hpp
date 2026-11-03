// minedict - BSD 3-Clause License; see README.
//
// Composes what the user sees: banners, entries, search results, errors.
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "minedict/cli/ansi.hpp"
#include "minedict/cli/dictionary.hpp"
#include "minedict/cli/render.hpp"
#include "minedict/cli/shelf.hpp"
#include "minedict/cli/theme.hpp"

namespace minedict::cli {

class Presenter {
 public:
  Presenter(const Theme& theme, ColorMode color, std::size_t width) noexcept;

  [[nodiscard]] const Theme& theme() const noexcept { return *theme_; }
  [[nodiscard]] ColorMode color() const noexcept { return color_; }
  [[nodiscard]] std::size_t width() const noexcept { return width_; }
  void set_width(std::size_t width) noexcept;
  void set_css(bool enabled) noexcept { css_enabled_ = enabled; }
  [[nodiscard]] bool css_enabled() const noexcept { return css_enabled_; }

  [[nodiscard]] HtmlRenderer renderer(std::size_t indent, bool show_images, bool show_sounds = false) const noexcept;

  [[nodiscard]] std::string paint(std::string_view text, Role role) const;
  [[nodiscard]] std::string rule() const;

  /// Title bar for an entry: headword on the left, dictionary on the right.
  [[nodiscard]] std::string banner(std::string_view headword, std::string_view title) const;

  [[nodiscard]] std::string entry(const Dictionary& dictionary, const Entry& entry,
                                  const HtmlRenderer& renderer) const;
  [[nodiscard]] std::string entries(const Shelf::LookupOutcome& outcome, const HtmlRenderer& renderer) const;

  [[nodiscard]] std::string not_found(std::string_view word, std::span<const Shelf::Candidate> suggestions,
                                      std::span<const Shelf::Match> prefixes) const;

  [[nodiscard]] std::string matches(std::string_view pattern, std::span<const Shelf::Match> matches,
                                    bool numbered, bool show_dictionary) const;

  [[nodiscard]] std::string info(const Dictionary& dictionary) const;
  [[nodiscard]] std::string resource_summary(const Dictionary& dictionary, std::string_view name,
                                             std::size_t bytes) const;

  [[nodiscard]] std::string problem(std::string_view message) const;
  [[nodiscard]] std::string warning(std::string_view message) const;
  [[nodiscard]] std::string note(std::string_view message) const;

 private:
  [[nodiscard]] std::string highlight(std::string_view key, std::string_view pattern) const;

  const Theme* theme_;
  ColorMode color_;
  std::size_t width_;
  bool css_enabled_{true};
};

}  // namespace minedict::cli
