cmake_minimum_required(VERSION 3.16)
project(uuuapp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# --- libusb ---
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBUSB libusb-1.0)
endif()

if(NOT LIBUSB_FOUND)
    find_library(LIBUSB_LIBRARIES NAMES usb-1.0 libusb-1.0 usb)
    find_path(LIBUSB_INCLUDE_DIRS NAMES libusb.h PATH_SUFFIXES libusb-1.0)
    if(LIBUSB_LIBRARIES AND LIBUSB_INCLUDE_DIRS)
        set(LIBUSB_FOUND TRUE)
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
    qt6_add_executable(uuuapp WIN32 ${SOURCES} ${HEADERS})
elseif(APPLE)
    set(MACOSX_BUNDLE_BUNDLE_NAME "UUU Flash Tool")
    qt6_add_executable(uuuapp MACOSX_BUNDLE ${SOURCES} ${HEADERS})
else()
    qt6_add_executable(uuuapp ${SOURCES} ${HEADERS})
endif()

target_link_libraries(uuuapp PRIVATE Qt6::Core Qt6::Widgets)

if(LIBUSB_FOUND)
    target_include_directories(uuuapp PRIVATE ${LIBUSB_INCLUDE_DIRS})
    target_link_libraries(uuuapp PRIVATE ${LIBUSB_LIBRARIES})
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
