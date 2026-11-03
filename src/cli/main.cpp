// minedict - BSD 3-Clause License; see README.
//
// minedict: a terminal dictionary for MDX/MDD files.
#include <string_view>
#include <utility>
#include <vector>

#include "minedict/cli/app.hpp"
#include "minedict/cli/compat.hpp"
#include "minedict/cli/options.hpp"

int main(int argc, char** argv) {
  using namespace minedict::cli;

  const std::vector<std::string_view> args(argv + 1, argv + argc);
  auto options = parse_options(args);
  if (!options) {
    minedict::cli::println(stderr, "minedict: {}", options.error().message);
    minedict::cli::println(stderr, "Try 'minedict --help' for usage.");
    return App::exit_usage;
  }
  return App{std::move(*options)}.run();
}
