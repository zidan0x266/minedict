# minedict - a terminal dictionary for MDX/MDD files, written in C++23.
#
# Configure from the repository root with:   cmake -S src -B build
#
# Layout:
#   include/minedict/        public headers of the core library (namespace minedict)
#   include/minedict/cli/    headers of the terminal front end (namespace minedict::cli)
#   core/                    the reader and index: header, blocks, LZO, keys, records
#   cli/                     rendering, Dracula theme, REPL, commands, main.cpp
cmake_minimum_required(VERSION 3.24)

project(minedict
    VERSION 1.0.0
    DESCRIPTION "A terminal dictionary for MDX/MDD files"
    HOMEPAGE_URL "https://github.com/zidan0x266/minedict"
    LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(MINEDICT_BUILD_CLI "Build the minedict executable" ON)
option(MINEDICT_WITH_READLINE "Use GNU readline or libedit for the interactive mode" ON)
option(MINEDICT_WITH_ICONV "Decode legacy code pages (GBK, Big5) through iconv" ON)
option(MINEDICT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

include(Toolchain)
include(Warnings)

add_subdirectory(core)
if(MINEDICT_BUILD_CLI)
    add_subdirectory(cli)
endif()

# ---- install ---------------------------------------------------------------
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
install(EXPORT minedictTargets
    NAMESPACE minedict::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/minedict)
configure_package_config_file(cmake/minedictConfig.cmake.in
    "${PROJECT_BINARY_DIR}/minedictConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/minedict)
write_basic_package_version_file("${PROJECT_BINARY_DIR}/minedictConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion)
install(FILES "${PROJECT_BINARY_DIR}/minedictConfig.cmake" "${PROJECT_BINARY_DIR}/minedictConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/minedict)
