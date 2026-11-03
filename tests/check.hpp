// minedict - a small test framework.
//
//   TEST(suite, name) { CHECK(x); CHECK_EQ(a, b); REQUIRE(ptr != nullptr); }
//
// Each test file is compiled with check.cpp, which provides main(). A test
// passes when none of its checks fail; REQUIRE leaves the test early.
// `SKIP("why")` marks a test as skipped. Run with `--list` to list tests or
// with a substring to run a subset.
#pragma once

#include <concepts>
#include <cstddef>
#include <format>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace minedict::testing {

struct TestCase {
  std::string name;
  std::function<void()> body;
};

std::vector<TestCase>& registry();
bool register_test(std::string name, std::function<void()> body);
void report_failure(std::string_view expression, std::string_view details, std::source_location where);
void report_skip(std::string_view reason);

/// Render a value for a failure message, whatever its type.
template <class T>
std::string describe(const T& value) {
  if constexpr (std::same_as<T, std::byte>) {
    return std::format("{:#04x}", std::to_integer<unsigned>(value));
  } else if constexpr (std::same_as<T, bool>) {
    return value ? "true" : "false";
  } else if constexpr (std::is_enum_v<T>) {
    return std::format("enum({})", static_cast<long long>(std::to_underlying(value)));
  } else if constexpr (std::formattable<T, char>) {
    return std::format("{}", value);
  } else if constexpr (requires { std::begin(value); std::end(value); value.size(); }) {
    std::string out = "[";
    std::size_t count = 0;
    for (const auto& item : value) {
      if (count++ > 0) out += ", ";
      if (count > 12) {
        out += "...";
        break;
      }
      out += describe(item);
    }
    return out + std::format("] ({} items)", value.size());
  } else {
    return "<value>";
  }
}

template <class A, class B>
bool check_equal(const A& a, const B& b, std::string_view expression, std::source_location where) {
  if (a == b) return true;
  report_failure(expression, std::format("left:  {}\n    right: {}", describe(a), describe(b)), where);
  return false;
}

template <class A, class B>
bool check_not_equal(const A& a, const B& b, std::string_view expression, std::source_location where) {
  if (!(a == b)) return true;
  report_failure(expression, std::format("both:  {}", describe(a)), where);
  return false;
}

inline bool check_true(bool value, std::string_view expression, std::source_location where) {
  if (value) return true;
  report_failure(expression, "", where);
  return false;
}

}  // namespace minedict::testing

#define MINEDICT_CONCAT_(a, b) a##b
#define MINEDICT_CONCAT(a, b) MINEDICT_CONCAT_(a, b)

#define TEST(suite, name)                                                                                   \
  static void MINEDICT_CONCAT(test_##suite##_##name, _body)();                                              \
  static const bool MINEDICT_CONCAT(test_##suite##_##name, _registered) =                                   \
      ::minedict::testing::register_test(#suite "." #name, &MINEDICT_CONCAT(test_##suite##_##name, _body)); \
  static void MINEDICT_CONCAT(test_##suite##_##name, _body)()

#define CHECK(expr) ::minedict::testing::check_true(static_cast<bool>(expr), #expr, std::source_location::current())
#define CHECK_FALSE(expr) \
  ::minedict::testing::check_true(!static_cast<bool>(expr), "not " #expr, std::source_location::current())
#define CHECK_EQ(a, b) ::minedict::testing::check_equal((a), (b), #a " == " #b, std::source_location::current())
#define CHECK_NE(a, b) ::minedict::testing::check_not_equal((a), (b), #a " != " #b, std::source_location::current())

#define REQUIRE(expr) \
  do {                \
    if (!CHECK(expr)) return; \
  } while (false)
#define REQUIRE_EQ(a, b) \
  do {                   \
    if (!CHECK_EQ(a, b)) return; \
  } while (false)

#define SKIP(reason)                           \
  do {                                         \
    ::minedict::testing::report_skip(reason); \
    return;                                    \
  } while (false)
