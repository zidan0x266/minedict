# The program: the core library and, by default, the terminal front end.
#
#   include/minedict/        core headers (namespace minedict)
#   include/minedict/cli/    front-end headers (namespace minedict::cli)
#   core/                    the reader and index: header, blocks, inflate, LZO, keys, records
#   cli/                     rendering, Dracula theme, REPL, commands, main.cpp

# A generated header carrying the project version.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/include/minedict/version.hpp.in"
               "${PROJECT_BINARY_DIR}/generated/minedict/version.hpp" @ONLY)

add_subdirectory(core)
if(MINEDICT_BUILD_CLI)
    add_subdirectory(cli)
endif()
