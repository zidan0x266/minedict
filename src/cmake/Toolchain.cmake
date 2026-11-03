# Compiler and standard-library requirements for minedict.
include(CheckCXXSourceCompiles)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

# Fail early with a readable message when the standard library lacks the
# C++23 pieces the code leans on.
set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX23_STANDARD_COMPILE_OPTION}")
check_cxx_source_compiles("
#include <expected>
#include <format>
#include <ranges>
#include <span>
#include <string>
int main() {
  std::expected<int, std::string> value{1};
  return std::format(\"{}\", *value).size() == 1 ? 0 : 1;
}
" MINEDICT_TOOLCHAIN_OK)
unset(CMAKE_REQUIRED_FLAGS)
if(NOT MINEDICT_TOOLCHAIN_OK)
    message(FATAL_ERROR
        "minedict needs a C++23 standard library with <expected> and <format>: "
        "GCC 13+, Clang 17+ with libc++ 17+, or Xcode 15.3+.")
endif()

# A generated header carrying the project version.
configure_file("${PROJECT_SOURCE_DIR}/include/minedict/version.hpp.in"
               "${PROJECT_BINARY_DIR}/generated/minedict/version.hpp" @ONLY)
