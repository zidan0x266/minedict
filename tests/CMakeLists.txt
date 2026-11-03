# Unit tests, built on the tiny framework in check.hpp (no third-party code).
# Each suite is one executable registered with CTest; run them all with
# `ctest --test-dir build`, or one directly: `build/bin/test_archive [filter]`.
add_library(minedict_check STATIC check.cpp)
target_include_directories(minedict_check PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(minedict_check PUBLIC minedict::core PRIVATE minedict::warnings)

function(minedict_add_test name)
    cmake_parse_arguments(ARG "" "" "LIBRARIES" ${ARGN})
    add_executable(${name} ${name}.cc)
    target_link_libraries(${name} PRIVATE minedict_check ${ARG_LIBRARIES} minedict::warnings)
    target_compile_definitions(${name} PRIVATE MINEDICT_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

foreach(suite bytes checksum ripemd128 lzo inflate unicode keys header compression archive index)
    minedict_add_test(test_${suite})
endforeach()

if(MINEDICT_BUILD_CLI)
    foreach(suite text html css render theme options dictionary)
        minedict_add_test(test_cli_${suite} LIBRARIES minedict::cli)
    endforeach()
endif()
