// minedict - BSD 3-Clause License; see README.
//
// Ties options, dictionaries and presentation together into commands.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "minedict/cli/ansi.hpp"
#include "minedict/cli/options.hpp"
#include "minedict/cli/presenter.hpp"
#include "minedict/cli/render.hpp"
#include "minedict/cli/shelf.hpp"
#include "minedict/cli/theme.hpp"

namespace minedict::cli {

class App {
 public:
  static constexpr int exit_ok = 0;
  static constexpr int exit_not_found = 1;
  static constexpr int exit_usage = 2;
  static constexpr int exit_no_dictionary = 3;

  explicit App(Options options);

  [[nodiscard]] int run();

 private:
  [[nodiscard]] int load_dictionaries();
  [[nodiscard]] int lookup();
  [[nodiscard]] int batch();
  [[nodiscard]] int search();
  [[nodiscard]] int list();
  [[nodiscard]] int info();
  [[nodiscard]] int extract();
  [[nodiscard]] int repl();

  /// Append the result of looking `word` up to `out`; true when found.
  bool append_lookup(std::string_view word, const Presenter& presenter, const HtmlRenderer& renderer,
                     std::string& out) const;

  Options options_;
  Theme theme_;
  ColorMode color_{ColorMode::none};
  std::size_t width_{80};
  Shelf shelf_;
};

}  // namespace minedict::cli
