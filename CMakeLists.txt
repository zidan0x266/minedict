# minedict - a terminal dictionary for MDX/MDD files, written in C++23.
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
#   ctest --test-dir build
#
# src/       the program: core reader + index (namespace minedict) and the
#            terminal front end (namespace minedict::cli)
# tests/     unit tests
# examples/  a toy dictionary and its terminal stylesheet
# macos/     Minedict.app, the macOS front end (-DMINEDICT_BUILD_MACOS=ON)
cmake_minimum_required(VERSION 3.24)

project(minedict
    VERSION 1.0.0
    DESCRIPTION "A terminal dictionary for MDX/MDD files"
    HOMEPAGE_URL "https://github.com/zidan0x266/minedict"
    LANGUAGES CXX)

option(MINEDICT_BUILD_CLI "Build the minedict executable" ON)
option(MINEDICT_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(MINEDICT_WITH_READLINE "Use GNU readline or libedit for the interactive mode" ON)
option(MINEDICT_WITH_ICONV "Decode legacy code pages (GBK, Big5) through iconv" ON)
option(MINEDICT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(MINEDICT_BUILD_MACOS "Build Minedict.app, the macOS front end (needs the CLI library)" OFF)
if(MINEDICT_BUILD_MACOS AND NOT MINEDICT_BUILD_CLI)
    message(FATAL_ERROR "MINEDICT_BUILD_MACOS needs MINEDICT_BUILD_CLI: the app is built on minedict::cli")
endif()
if(MINEDICT_BUILD_MACOS)
    enable_language(OBJCXX)  # the bridge in macos/ and its test in tests/
endif()

# ---- toolchain -------------------------------------------------------------
include(CheckCXXSourceCompiles)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

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

# ---- shared warning set ------------------------------------------------------
add_library(minedict_warnings INTERFACE)
add_library(minedict::warnings ALIAS minedict_warnings)
if(MSVC)
    target_compile_options(minedict_warnings INTERFACE /W4 /permissive- /utf-8)
    if(MINEDICT_WARNINGS_AS_ERRORS)
        target_compile_options(minedict_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(minedict_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference -Wimplicit-fallthrough)
    if(MINEDICT_WARNINGS_AS_ERRORS)
        target_compile_options(minedict_warnings INTERFACE -Werror)
    endif()
endif()

# ---- the program and its tests ----------------------------------------------
add_subdirectory(src)
if(MINEDICT_BUILD_MACOS)
    add_subdirectory(macos)
endif()
if(MINEDICT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- install --------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS minedict_core EXPORT minedictTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${PROJECT_BINARY_DIR}/generated/minedict/version.hpp"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/minedict)
if(MINEDICT_BUILD_CLI)
    install(TARGETS minedict RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(EXPORT minedictTargets NAMESPACE minedict:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/minedict)

file(WRITE "${PROJECT_BINARY_DIR}/minedictConfig.cmake.in" [=[
@PACKAGE_INIT@
include(CMakeFindDependencyMacro)
if(@MINEDICT_WITH_ICONV@)
    find_dependency(Iconv)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/minedictTargets.cmake")
check_required_components(minedict)
]=])
configure_package_config_file("${PROJECT_BINARY_DIR}/minedictConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/minedictConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/minedict)
write_basic_package_version_file("${PROJECT_BINARY_DIR}/minedictConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion)
install(FILES "${PROJECT_BINARY_DIR}/minedictConfig.cmake" "${PROJECT_BINARY_DIR}/minedictConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/minedict)
