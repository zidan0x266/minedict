#include "check.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

namespace minedict::testing {
namespace {

struct State {
  std::size_t failures_in_current{0};
  bool skipped_current{false};
  std::string skip_reason;
};

State& state() {
  static State instance;
  return instance;
}

}  // namespace

std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

bool register_test(std::string name, std::function<void()> body) {
  registry().push_back(TestCase{.name = std::move(name), .body = std::move(body)});
  return true;
}

void report_failure(std::string_view expression, std::string_view details, std::source_location where) {
  state().failures_in_current++;
  std::printf("    FAILED  %s:%u\n    check: %.*s\n", where.file_name(), where.line(), static_cast<int>(expression.size()),
              expression.data());
  if (!details.empty()) std::printf("    %.*s\n", static_cast<int>(details.size()), details.data());
}

void report_skip(std::string_view reason) {
  state().skipped_current = true;
  state().skip_reason = std::string(reason);
}

}  // namespace minedict::testing

int main(int argc, char** argv) {
  using namespace minedict::testing;
  std::string filter;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--list") {
      for (const TestCase& test : registry()) std::printf("%s\n", test.name.c_str());
      return 0;
    }
    filter = std::string(arg);
  }

  std::size_t passed = 0;
  std::size_t failed = 0;
  std::size_t skipped = 0;
  const auto started = std::chrono::steady_clock::now();
  for (const TestCase& test : registry()) {
    if (!filter.empty() && test.name.find(filter) == std::string::npos) continue;
    state() = {};
    try {
      test.body();
    } catch (const std::exception& e) {
      report_failure("no exception", e.what(), std::source_location::current());
    } catch (...) {
      report_failure("no exception", "unknown exception", std::source_location::current());
    }
    if (state().skipped_current) {
      ++skipped;
      std::printf("  skip  %s (%s)\n", test.name.c_str(), state().skip_reason.c_str());
    } else if (state().failures_in_current == 0) {
      ++passed;
      std::printf("  ok    %s\n", test.name.c_str());
    } else {
      ++failed;
      std::printf("  FAIL  %s\n", test.name.c_str());
    }
  }
  const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started);
  std::printf("%zu passed, %zu failed, %zu skipped in %.0f ms\n", passed, failed, skipped, elapsed.count());
  return failed == 0 ? 0 : 1;
}
