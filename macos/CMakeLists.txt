# Minedict.app - the macOS front end: SwiftUI over minedict::cli through an
# Objective-C bridge.
#
#   cmake -S . -B build -DMINEDICT_BUILD_MACOS=ON && cmake --build build
#   open build/bin/Minedict.app
#
# The Swift sources are compiled by swiftc directly, so any generator works;
# the bundle, the link step and the install rules are CMake's.
if(NOT APPLE)
    message(FATAL_ERROR "MINEDICT_BUILD_MACOS needs macOS")
endif()

# ---- toolchain ---------------------------------------------------------------
execute_process(COMMAND xcrun --find swiftc
    OUTPUT_VARIABLE MINEDICT_SWIFTC OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _swiftc_status)
if(NOT _swiftc_status EQUAL 0)
    message(FATAL_ERROR "Minedict.app needs swiftc (Xcode or the Command Line Tools)")
endif()

if(CMAKE_OSX_SYSROOT AND IS_DIRECTORY "${CMAKE_OSX_SYSROOT}")
    set(MINEDICT_MACOS_SDK "${CMAKE_OSX_SYSROOT}")
else()
    execute_process(COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE MINEDICT_MACOS_SDK OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

# The macOS version the binaries run on: what the user asked for, else the
# compiler's own default, so the Swift objects match the C++ ones.
if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(MINEDICT_MACOS_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}")
else()
    execute_process(COMMAND ${CMAKE_OBJCXX_COMPILER} -x objective-c++ -isysroot "${MINEDICT_MACOS_SDK}" -E -dM -
        INPUT_FILE /dev/null OUTPUT_VARIABLE _macros ERROR_QUIET)
    if(_macros MATCHES "__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ ([0-9]+)")
        math(EXPR _major "${CMAKE_MATCH_1} / 10000")
        math(EXPR _minor "(${CMAKE_MATCH_1} / 100) % 100")
        set(MINEDICT_MACOS_TARGET "${_major}.${_minor}")
    else()
        set(MINEDICT_MACOS_TARGET "14.0")
    endif()
endif()
if(MINEDICT_MACOS_TARGET VERSION_LESS 14.0)
    message(FATAL_ERROR "Minedict.app needs macOS 14 or newer; set CMAKE_OSX_DEPLOYMENT_TARGET to 14.0 or above")
endif()

if(CMAKE_OSX_ARCHITECTURES)
    list(GET CMAKE_OSX_ARCHITECTURES 0 MINEDICT_MACOS_ARCH)
else()
    set(MINEDICT_MACOS_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()
message(STATUS "minedict: Minedict.app for ${MINEDICT_MACOS_ARCH}-apple-macosx${MINEDICT_MACOS_TARGET}")

# ---- the bridge: Objective-C over minedict::cli --------------------------------
add_library(minedict_bridge STATIC Bridge/MinedictBridge.mm)
add_library(minedict::bridge ALIAS minedict_bridge)
target_include_directories(minedict_bridge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Bridge")
target_link_libraries(minedict_bridge PUBLIC minedict::cli "-framework Foundation" PRIVATE minedict::warnings)
target_compile_options(minedict_bridge PRIVATE -fobjc-arc)
set_target_properties(minedict_bridge PROPERTIES
    OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)

# ---- the Swift sources ------------------------------------------------------------
set(MINEDICT_APP_SOURCES
    App/MinedictApp.swift
    App/Settings.swift
    App/Library.swift
    App/ContentView.swift
    App/SidebarView.swift
    App/EntryView.swift
    App/EntryPage.swift
    App/EntryWebView.swift
    App/DictionariesView.swift
    App/SoundPlayer.swift)
set(_swift_dir "${CMAKE_CURRENT_BINARY_DIR}/swift")
file(MAKE_DIRECTORY "${_swift_dir}")
set(_swift_inputs)
set(_swift_objects)
foreach(source IN LISTS MINEDICT_APP_SOURCES)
    list(APPEND _swift_inputs "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    get_filename_component(_stem "${source}" NAME_WE)
    list(APPEND _swift_objects "${_swift_dir}/${_stem}.o")
endforeach()

add_custom_command(
    OUTPUT ${_swift_objects}
    COMMAND ${MINEDICT_SWIFTC} -c -module-name Minedict -parse-as-library -swift-version 5
            -target "${MINEDICT_MACOS_ARCH}-apple-macosx${MINEDICT_MACOS_TARGET}"
            -sdk "${MINEDICT_MACOS_SDK}"
            -import-objc-header "${CMAKE_CURRENT_SOURCE_DIR}/Bridge/MinedictBridge.h"
            "$<IF:$<CONFIG:Debug>,-Onone;-g,-O>"
            ${_swift_inputs}
    WORKING_DIRECTORY "${_swift_dir}"
    DEPENDS ${_swift_inputs} "${CMAKE_CURRENT_SOURCE_DIR}/Bridge/MinedictBridge.h"
    COMMENT "Compiling the Swift sources of Minedict.app"
    COMMAND_EXPAND_LISTS
    VERBATIM)
set_source_files_properties(${_swift_objects} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

# ---- the icon ---------------------------------------------------------------------
# Painted at build time by Icon/DrawIcon.swift; iconutil packs the iconset into AppIcon.icns.
set(_icon_dir "${CMAKE_CURRENT_BINARY_DIR}/icon")
file(MAKE_DIRECTORY "${_icon_dir}")
add_custom_command(
    OUTPUT "${_icon_dir}/DrawIcon"
    COMMAND ${MINEDICT_SWIFTC} -O -target "${MINEDICT_MACOS_ARCH}-apple-macosx${MINEDICT_MACOS_TARGET}"
            -sdk "${MINEDICT_MACOS_SDK}" "${CMAKE_CURRENT_SOURCE_DIR}/Icon/DrawIcon.swift" -o "${_icon_dir}/DrawIcon"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Icon/DrawIcon.swift"
    COMMENT "Compiling the icon painter"
    VERBATIM)
add_custom_command(
    OUTPUT "${_icon_dir}/AppIcon.icns"
    COMMAND "${_icon_dir}/DrawIcon" "${_icon_dir}"
    COMMAND iconutil -c icns "${_icon_dir}/AppIcon.iconset" -o "${_icon_dir}/AppIcon.icns"
    DEPENDS "${_icon_dir}/DrawIcon"
    COMMENT "Drawing AppIcon.icns"
    VERBATIM)
set_source_files_properties("${_icon_dir}/AppIcon.icns" PROPERTIES GENERATED TRUE MACOSX_PACKAGE_LOCATION Resources)

# ---- the bundle -------------------------------------------------------------------
add_executable(Minedict MACOSX_BUNDLE ${_swift_objects} "${_icon_dir}/AppIcon.icns")
set_target_properties(Minedict PROPERTIES
    OUTPUT_NAME Minedict
    LINKER_LANGUAGE OBJCXX
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_ICON_FILE "AppIcon"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.zidan0x266.minedict"
    MACOSX_BUNDLE_BUNDLE_NAME "Minedict"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
target_link_libraries(Minedict PRIVATE minedict::bridge objc
    "-framework AppKit" "-framework SwiftUI" "-framework WebKit" "-framework AVFoundation"
    "-framework UniformTypeIdentifiers")
# The Swift objects name their runtime libraries themselves; this is where the
# SDK keeps them. Libraries only the terminal front end uses (readline) are
# dropped from the load commands.
target_link_options(Minedict PRIVATE "-L${MINEDICT_MACOS_SDK}/usr/lib/swift" "LINKER:-rpath,/usr/lib/swift"
    "LINKER:-dead_strip_dylibs")
# Signing comes last, once the executable and the icon are both in the bundle.
set(_signed "${CMAKE_CURRENT_BINARY_DIR}/Minedict.signed")
add_custom_command(
    OUTPUT "${_signed}"
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:Minedict>"
    COMMAND ${CMAKE_COMMAND} -E touch "${_signed}"
    DEPENDS Minedict "$<TARGET_FILE:Minedict>" "${_icon_dir}/AppIcon.icns"
    COMMENT "Signing Minedict.app (ad hoc)"
    VERBATIM)
add_custom_target(minedict_app ALL DEPENDS "${_signed}")

install(TARGETS Minedict BUNDLE DESTINATION Applications)
