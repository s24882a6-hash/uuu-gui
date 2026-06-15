cmake_minimum_required(VERSION 3.16)
project(uuuapp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

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

# --- Sources ---
set(SOURCES
    main.cpp
    mainwindow.cpp
    firmwarepreset.cpp
    presetdialog.cpp
    devicemonitor.cpp
    flashworker.cpp
    deviceitemwidget.cpp
    logdialog.cpp
)

set(HEADERS
    mainwindow.h
    firmwarepreset.h
    presetdialog.h
    devicemonitor.h
    flashworker.h
    deviceitemwidget.h
    logdialog.h
)

if(WIN32)
    set(APP_ICON_WIN "${CMAKE_CURRENT_SOURCE_DIR}/icons/uuuapp.rc")
    qt6_add_executable(uuuapp WIN32 ${SOURCES} ${HEADERS} ${APP_ICON_WIN})
elseif(APPLE)
    set(MACOSX_BUNDLE_BUNDLE_NAME "UUU Flash Tool")
    set(APP_ICON_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/icons/uuuapp.icns")
    if(EXISTS "${APP_ICON_MACOS}")
        set(MACOSX_BUNDLE_ICON_FILE uuuapp.icns)
        set_source_files_properties(${APP_ICON_MACOS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        qt6_add_executable(uuuapp MACOSX_BUNDLE ${SOURCES} ${HEADERS} ${APP_ICON_MACOS})
    else()
        qt6_add_executable(uuuapp MACOSX_BUNDLE ${SOURCES} ${HEADERS})
    endif()
else()
    qt6_add_executable(uuuapp ${SOURCES} ${HEADERS})
endif()

qt6_add_resources(uuuapp "resources" PREFIX "/" FILES icons/uuuapp.svg)
qt6_add_resources(uuuapp "translations" PREFIX "/i18n" FILES translations/uuuapp_ru.qm)

target_link_libraries(uuuapp PRIVATE Qt6::Core Qt6::Widgets Qt6::Svg)

if(LIBUSB_FOUND)
    target_link_libraries(uuuapp PRIVATE PkgConfig::LIBUSB)
    target_compile_definitions(uuuapp PRIVATE HAVE_LIBUSB)
endif()

if(WIN32)
    target_link_libraries(uuuapp PRIVATE setupapi)
endif()

if(APPLE)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(uuuapp PRIVATE ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
endif()

# Install
install(TARGETS uuuapp
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
