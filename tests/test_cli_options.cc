#include <string>
#include <string_view>
#include <vector>

#include "check.hpp"
#include "minedict/cli/options.hpp"

using namespace minedict;
using namespace minedict::cli;

namespace {
Result<Options> parse(std::vector<std::string_view> args) { return parse_options(args); }
}  // namespace

TEST(options, lookup_is_the_default_command) {
  const auto options = parse({"-d", "a.mdx", "cake", "bun"});
  REQUIRE(options.has_value());
  CHECK_EQ(options->command, Command::lookup);
  CHECK_FALSE(options->command_explicit);
  CHECK_EQ(options->dictionaries.size(), 1U);
  CHECK_EQ(options->dictionaries[0].string(), "a.mdx");
  CHECK_EQ(options->arguments, (std::vector<std::string>{"cake", "bun"}));
  CHECK(options->use_css);
  CHECK(options->pager);
  CHECK_FALSE(options->show_sounds);
}

TEST(options, bare_dictionary_files_and_commands) {
  const auto options = parse({"oxford.mdx", "sounds.MDD", "search", "-c", "cak", "-n", "20"});
  REQUIRE(options.has_value());
  CHECK_EQ(options->command, Command::search);
  CHECK(options->command_explicit);
  CHECK_EQ(options->dictionaries.size(), 2U);
  CHECK_EQ(options->search_mode, SearchMode::contains);
  CHECK_EQ(options->arguments, std::vector<std::string>{"cak"});
  CHECK_EQ(options->limit.value_or(0), 20U);

  const auto lookup_word = parse({"-d", "a.mdx", "lookup", "list"});  // "list" is a word here
  REQUIRE(lookup_word.has_value());
  CHECK_EQ(lookup_word->command, Command::lookup);
  CHECK_EQ(lookup_word->arguments, std::vector<std::string>{"list"});
}

TEST(options, output_flags) {
  const auto options = parse({"--raw", "--width", "60", "--no-pager", "--theme", "plain", "--color", "never",
                              "--no-follow", "--no-images", "--sounds", "--no-css", "-v", "list"});
  REQUIRE(options.has_value());
  CHECK_EQ(options->command, Command::list);
  CHECK(options->raw);
  CHECK_EQ(options->width.value_or(0), 60U);
  CHECK_FALSE(options->pager);
  CHECK_EQ(options->theme, "plain");
  CHECK_EQ(options->color, ColorChoice::never);
  CHECK_FALSE(options->follow_links);
  CHECK_FALSE(options->show_images);
  CHECK(options->show_sounds);
  CHECK_FALSE(options->use_css);
  CHECK(options->verbose);

  const auto extract = parse({"-d", "r.mdd", "extract", "\\a.mp3", "-o", "a.mp3"});
  REQUIRE(extract.has_value());
  CHECK_EQ(extract->command, Command::extract);
  REQUIRE(extract->output.has_value());
  CHECK_EQ(extract->output->string(), "a.mp3");
}

TEST(options, help_version_and_errors) {
  CHECK_EQ(parse({"--help"}).value().command, Command::help);
  CHECK_EQ(parse({"-V"}).value().command, Command::version);
  CHECK_EQ(parse({}).value().command, Command::repl);

  const auto bad_number = parse({"--width", "wide"});
  REQUIRE(!bad_number.has_value());
  CHECK_EQ(bad_number.error().kind, ErrorKind::invalid_argument);
  CHECK_FALSE(parse({"--no-such-flag"}).has_value());
  CHECK_FALSE((parse({"--color", "sometimes"}).has_value()));
  CHECK_FALSE(parse({"-d"}).has_value());  // missing value
}

TEST(options, command_names_and_usage) {
  CHECK_EQ(parse_command("info"), Command::info);
  CHECK_EQ(parse_command("Cake"), std::nullopt);
  CHECK_EQ(to_string(Command::extract), "extract");
  CHECK(usage_text("minedict").contains("minedict [options] search"));
  CHECK(usage_text("minedict").contains(version_string()));
  CHECK_FALSE(version_string().empty());
}
