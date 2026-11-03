# An interface target that turns on the warning set used across minedict.
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
