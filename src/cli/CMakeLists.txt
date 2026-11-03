# minedict::cli - the terminal front end, and the `minedict` executable.
add_library(minedict_cli STATIC
    ansi.cpp
    app.cpp
    css.cpp
    dictionary.cpp
    html.cpp
    options.cpp
    presenter.cpp
    render.cpp
    repl.cpp
    shelf.cpp
    terminal.cpp
    text.cpp
    theme.cpp
)
add_library(minedict::cli ALIAS minedict_cli)
target_include_directories(minedict_cli PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(minedict_cli PUBLIC minedict::core PRIVATE $<BUILD_INTERFACE:minedict::warnings>)
target_compile_features(minedict_cli PUBLIC cxx_std_23)

# ---- line editing for the interactive mode ----------------------------------
set(MINEDICT_LINE_EDITOR "none")
if(MINEDICT_WITH_READLINE)
    set(_readline_hints)
    if(APPLE)
        list(APPEND _readline_hints /opt/homebrew/opt/readline /usr/local/opt/readline)
    endif()
    find_path(MINEDICT_READLINE_INCLUDE_DIR NAMES readline/readline.h HINTS ${_readline_hints} PATH_SUFFIXES include)
    find_library(MINEDICT_READLINE_LIBRARY NAMES readline HINTS ${_readline_hints} PATH_SUFFIXES lib)
    if(MINEDICT_READLINE_INCLUDE_DIR AND MINEDICT_READLINE_LIBRARY)
        target_include_directories(minedict_cli PRIVATE ${MINEDICT_READLINE_INCLUDE_DIR})
        target_link_libraries(minedict_cli PUBLIC ${MINEDICT_READLINE_LIBRARY})
        target_compile_definitions(minedict_cli PRIVATE MINEDICT_HAVE_READLINE=1)
        # Apple's SDK offers libedit behind readline's name.
        if(MINEDICT_READLINE_LIBRARY MATCHES "\\.tbd$" OR MINEDICT_READLINE_INCLUDE_DIR MATCHES "/SDKs/")
            set(MINEDICT_LINE_EDITOR "libedit (readline API)")
            target_compile_definitions(minedict_cli PRIVATE MINEDICT_READLINE_IS_LIBEDIT=1)
        else()
            set(MINEDICT_LINE_EDITOR "GNU readline")
        endif()
    else()
        find_path(MINEDICT_EDITLINE_INCLUDE_DIR NAMES editline/readline.h)
        find_library(MINEDICT_EDITLINE_LIBRARY NAMES edit)
        if(MINEDICT_EDITLINE_INCLUDE_DIR AND MINEDICT_EDITLINE_LIBRARY)
            target_include_directories(minedict_cli PRIVATE ${MINEDICT_EDITLINE_INCLUDE_DIR})
            target_link_libraries(minedict_cli PUBLIC ${MINEDICT_EDITLINE_LIBRARY})
            target_compile_definitions(minedict_cli PRIVATE
                MINEDICT_HAVE_READLINE=1 MINEDICT_READLINE_IS_LIBEDIT=1 MINEDICT_READLINE_HEADER_EDITLINE=1)
            set(MINEDICT_LINE_EDITOR "libedit")
        endif()
    endif()
endif()
message(STATUS "minedict: line editor ${MINEDICT_LINE_EDITOR}")

# ---- the executable ----------------------------------------------------------
add_executable(minedict main.cpp)
target_link_libraries(minedict PRIVATE minedict::cli minedict::warnings)
