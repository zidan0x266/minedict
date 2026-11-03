// minedict - BSD 3-Clause License; see README.
//
// The interactive mode: a prompt with history and tab completion over the
// headword index, plus a few slash commands.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "minedict/cli/ansi.hpp"
#include "minedict/cli/presenter.hpp"
#include "minedict/cli/shelf.hpp"
#include "minedict/cli/theme.hpp"

namespace minedict::cli {

class Repl {
 public:
  struct Settings {
    bool raw{false};
    bool follow_links{true};
    std::size_t limit{30};
    bool pager{true};
    bool show_images{true};
    bool show_sounds{false};
    bool use_css{true};
    std::optional<std::size_t> fixed_width;
  };

  Repl(const Shelf& shelf, Theme theme, ColorMode color, Settings settings);

  /// Run until the user quits. Returns the process exit code.
  [[nodiscard]] int run();

  /// Handle one line as if typed at the prompt (exposed for testing).
  void handle(std::string_view line);
  [[nodiscard]] bool running() const noexcept { return running_; }

 private:
  void refresh_width();
  void greet() const;
  void lookup(std::string_view word, bool raw);
  void search(std::string_view pattern, bool contains);
  void pick(std::size_t number);
  void command(std::string_view line);
  void show_help() const;
  void show_dictionaries() const;
  void show_info(std::optional<std::size_t> index) const;
  void say(std::string_view text) const;

  const Shelf& shelf_;
  Theme theme_;
  Presenter presenter_;
  Settings settings_;
  std::vector<Shelf::Match> last_matches_;
  bool running_{true};
};

}  // namespace minedict::cli
