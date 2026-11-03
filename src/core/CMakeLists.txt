# minedict::core - reads MDX/MDD files and indexes their keys.
add_library(minedict_core STATIC
    archive.cpp
    compression.cpp
    encoding.cpp
    header.cpp
    index.cpp
    inflate.cpp
    keys.cpp
    lzo.cpp
    mapped_file.cpp
    ripemd128.cpp
    unicode.cpp
)
add_library(minedict::core ALIAS minedict_core)
set_target_properties(minedict_core PROPERTIES OUTPUT_NAME minedict EXPORT_NAME core POSITION_INDEPENDENT_CODE ON)

target_sources(minedict_core PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${PROJECT_SOURCE_DIR}/include"
    FILES
        "${PROJECT_SOURCE_DIR}/include/minedict/archive.hpp"
        "${PROJECT_SOURCE_DIR}/include/minedict/bytes.hpp"
        "${PROJECT_SOURCE_DIR}/include/minedict/checksum.hpp"
        "${PROJECT_SOURCE_DIR}/include/minedict/compression.hpp"
        "${PROJECT_SOURCE_DIR}/include/minedict/encoding.hpp"
        "${PROJECT_SOURCE_DIR}/include/minedict/error.hpp"
        "${PROJECT_SOURCE_DIR}/include/minedict/header.hpp"
        "${PROJECT_SOURCE_DIR}/include/minedict/index.hpp"
        "${PROJECT_SOURCE_DIR}/include/minedict/inflate.hpp"
        "${PROJECT_SOURCE_DIR}/include/minedict/keys.hpp"
        "${PROJECT_SOURCE_DIR}/include/minedict/lzo.hpp"
        "${PROJECT_SOURCE_DIR}/include/minedict/mapped_file.hpp"
        "${PROJECT_SOURCE_DIR}/include/minedict/ripemd128.hpp"
        "${PROJECT_SOURCE_DIR}/include/minedict/unicode.hpp"
)
target_include_directories(minedict_core PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(minedict_core PUBLIC cxx_std_23)
target_link_libraries(minedict_core PRIVATE $<BUILD_INTERFACE:minedict::warnings>)

# Legacy code pages (GBK, Big5, ...) are decoded through iconv when present.
set(MINEDICT_HAVE_ICONV 0)
if(MINEDICT_WITH_ICONV)
    find_package(Iconv)
    if(Iconv_FOUND)
        set(MINEDICT_HAVE_ICONV 1)
        target_link_libraries(minedict_core PRIVATE Iconv::Iconv)
    else()
        message(STATUS "iconv not found: only UTF-8/UTF-16 dictionaries will open")
    endif()
endif()
target_compile_definitions(minedict_core PRIVATE MINEDICT_HAVE_ICONV=${MINEDICT_HAVE_ICONV})
message(STATUS "minedict: iconv support ${MINEDICT_HAVE_ICONV}")
