cmake_minimum_required(VERSION 3.16)
project(UUUFlashTool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# On macOS several libuuu dependencies (zstd, tinyxml2, openssl) are Homebrew
# keg-only, so their .pc files aren't on the default pkg-config search path.
# Add them automatically so a plain `cmake -B build` works without exporting
# PKG_CONFIG_PATH by hand. Must run before FetchContent pulls in libuuu.
if(APPLE)
    find_program(BREW_EXECUTABLE brew)
    if(BREW_EXECUTABLE)
        foreach(_pkg zstd tinyxml2 libusb openssl@3)
            execute_process(COMMAND ${BREW_EXECUTABLE} --prefix ${_pkg}
                OUTPUT_VARIABLE _prefix OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_prefix AND EXISTS "${_prefix}/lib/pkgconfig")
                set(ENV{PKG_CONFIG_PATH} "${_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
            endif()
        endforeach()
        if(NOT OPENSSL_ROOT_DIR)
            execute_process(COMMAND ${BREW_EXECUTABLE} --prefix openssl@3
                OUTPUT_VARIABLE OPENSSL_ROOT_DIR OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        endif()
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Svg)

# --- libusb ---
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBUSB IMPORTED_TARGET libusb-1.0)
endif()

if(NOT LIBUSB_FOUND)
    find_library(LIBUSB_LIB NAMES usb-1.0 libusb-1.0 usb)
    find_path(LIBUSB_INC NAMES libusb.h PATH_SUFFIXES libusb-1.0)
    if(LIBUSB_LIB AND LIBUSB_INC)
        set(LIBUSB_FOUND TRUE)
        add_library(PkgConfig::LIBUSB UNKNOWN IMPORTED)
        set_target_properties(PkgConfig::LIBUSB PROPERTIES
            IMPORTED_LOCATION "${LIBUSB_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB_INC}")
    endif()
endif()

if(NOT LIBUSB_FOUND)
    message(WARNING "libusb-1.0 not found — USB device monitoring will be disabled.\n"
                    "  Linux:   sudo apt install libusb-1.0-0-dev\n"
                    "  macOS:   brew install libusb\n"
                    "  Windows: install via vcpkg: vcpkg install libusb")
endif()

# --- libuuu (built from source, drives the uuu-helper worker) ---
# Vendored via FetchContent; SOURCE_SUBDIR builds only the static `uuc_s`
# library target, not the upstream CLI.
include(FetchContent)
FetchContent_Declare(mfgtools
    GIT_REPOSITORY    https://github.com/nxp-imx/mfgtools.git
    GIT_TAG           uuu_1.5.243
    GIT_SUBMODULES    ""        # we link system libraries, not bundled submodules
    GIT_SHALLOW       TRUE
    SOURCE_SUBDIR     libuuu
    # libuuu generates gitversion.h with POSIX shell commands that fail on
    # Windows; patch it to a portable cmake -E copy of a static header.
    PATCH_COMMAND     ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_libuuu.cmake
)
FetchContent_MakeAvailable(mfgtools)

# --- Sources ---
set(SOURCES
    main.cpp
    mainwindow.cpp
    firmwarepreset.cpp
    presetdialog.cpp
    settingsdialog.cpp
    devicemonitor.cpp
    flashworker.cpp
    deviceitemwidget.cpp
    logdialog.cpp
    apppaths.cpp
)

set(HEADERS
    mainwindow.h
    firmwarepreset.h
    presetdialog.h
    settingsdialog.h
    devicemonitor.h
    flashworker.h
    deviceitemwidget.h
    logdialog.h
    apppaths.h
)

if(WIN32)
    set(APP_ICON_WIN "${CMAKE_CURRENT_SOURCE_DIR}/icons/UUUFlashTool.rc")
    qt6_add_executable(UUUFlashTool WIN32 ${SOURCES} ${HEADERS} ${APP_ICON_WIN})
elseif(APPLE)
    set(MACOSX_BUNDLE_BUNDLE_NAME "UUU Flash Tool")
    set(APP_ICON_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/icons/UUUFlashTool.icns")
    if(EXISTS "${APP_ICON_MACOS}")
        set(MACOSX_BUNDLE_ICON_FILE UUUFlashTool.icns)
        set_source_files_properties(${APP_ICON_MACOS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        qt6_add_executable(UUUFlashTool MACOSX_BUNDLE ${SOURCES} ${HEADERS} ${APP_ICON_MACOS})
    else()
        qt6_add_executable(UUUFlashTool MACOSX_BUNDLE ${SOURCES} ${HEADERS})
    endif()
else()
    qt6_add_executable(UUUFlashTool ${SOURCES} ${HEADERS})
endif()

qt6_add_resources(UUUFlashTool "resources" PREFIX "/" FILES icons/UUUFlashTool.svg)
target_sources(UUUFlashTool PRIVATE translations/translations.qrc)

# --- Version from git tag ---
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    string(REGEX REPLACE "^v" "" GIT_VERSION "${GIT_VERSION}")
endif()
if(NOT GIT_VERSION OR GIT_VERSION STREQUAL "")
    set(GIT_VERSION "${PROJECT_VERSION}")
endif()
message(STATUS "App version: ${GIT_VERSION}")
target_compile_definitions(UUUFlashTool PRIVATE APP_VERSION="${GIT_VERSION}")

target_link_libraries(UUUFlashTool PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg)

if(LIBUSB_FOUND)
    target_link_libraries(UUUFlashTool PRIVATE PkgConfig::LIBUSB)
    target_compile_definitions(UUUFlashTool PRIVATE HAVE_LIBUSB)
endif()

if(WIN32)
    target_link_libraries(UUUFlashTool PRIVATE setupapi)
endif()

if(APPLE)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(UUUFlashTool PRIVATE ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
endif()

# --- uuu-helper (libuuu-backed flash/list worker, spawned by the GUI) ---
# uuc_s exposes no usage requirements, so we resupply libuuu's include dir and
# its transitive link dependencies here.
find_package(BZip2 REQUIRED)
find_package(ZLIB  REQUIRED)
find_package(OpenSSL)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBZSTD  REQUIRED IMPORTED_TARGET libzstd)
    pkg_check_modules(TINYXML2 REQUIRED IMPORTED_TARGET tinyxml2)
    pkg_check_modules(HELPER_LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
endif()

add_executable(uuu-helper helper/uuuhelper.cpp)
target_include_directories(uuu-helper PRIVATE "${mfgtools_SOURCE_DIR}/libuuu")
target_link_libraries(uuu-helper PRIVATE
    uuc_s
    PkgConfig::HELPER_LIBUSB
    PkgConfig::LIBZSTD
    PkgConfig::TINYXML2
    BZip2::BZip2
    ZLIB::ZLIB
)
if(OPENSSL_FOUND)
    target_link_libraries(uuu-helper PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
if(APPLE)
    target_link_libraries(uuu-helper PRIVATE
        "-framework IOKit" "-framework CoreFoundation" "-framework Security")
endif()

# Place the helper next to the app executable so applicationDirPath() finds it
# (on macOS this is UUUFlashTool.app/Contents/MacOS). Done via an always-run
# target so the copy refreshes even when only the helper was rebuilt.
add_custom_target(bundle-helper ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:uuu-helper> $<TARGET_FILE_DIR:UUUFlashTool>
    COMMENT "Bundling uuu-helper next to UUUFlashTool")
add_dependencies(bundle-helper uuu-helper UUUFlashTool)

# Install
install(TARGETS UUUFlashTool
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
install(TARGETS uuu-helper RUNTIME DESTINATION bin)
