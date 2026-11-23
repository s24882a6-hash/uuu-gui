cmake_minimum_required(VERSION 3.22.1)
project(uuuflash C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# ── Исходники зависимостей ────────────────────────────────────────────────────
# Качаются при первой конфигурации и проверяются по SHA256. SOURCE_SUBDIR
# указывает в никуда, чтобы FetchContent только распаковал архив, а не звал
# чужой CMakeLists: у libuuu он требует pkg-config, которого в NDK нет.
include(FetchContent)

FetchContent_Declare(libusb
    URL      https://github.com/libusb/libusb/releases/download/v1.0.29/libusb-1.0.29.tar.bz2
    URL_HASH SHA256=5977fc950f8d1395ccea9bd48c06b3f808fd3c2c961b44b0c2e6e29fc3a70a85
    SOURCE_SUBDIR _none)
FetchContent_Declare(zstd
    URL      https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
    SOURCE_SUBDIR _none)
FetchContent_Declare(bzip2
    URL      https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
    URL_HASH SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
    SOURCE_SUBDIR _none)
FetchContent_Declare(tinyxml2
    URL      https://github.com/leethomason/tinyxml2/archive/refs/tags/11.0.0.tar.gz
    URL_HASH SHA256=5556deb5081fb246ee92afae73efd943c889cef0cafea92b0b82422d6a18f289
    SOURCE_SUBDIR _none)
# Та же версия libuuu, что в настольном uuu-gui (../../CMakeLists.txt).
FetchContent_Declare(mfgtools
    URL      https://github.com/nxp-imx/mfgtools/archive/refs/tags/uuu_1.5.243.tar.gz
    URL_HASH SHA256=9ea8d3f98081372e957e04d748eb50ed3b23a128710f1c3549a3f84b5a33b94b
    SOURCE_SUBDIR _none)
FetchContent_MakeAvailable(libusb zstd bzip2 tinyxml2 mfgtools)

# ── libusb ───────────────────────────────────────────────────────────────────
# Список файлов и config.h — из android/jni/libusb.mk самой libusb.
set(U ${libusb_SOURCE_DIR}/libusb)
add_library(usb1 STATIC
    ${U}/core.c ${U}/descriptor.c ${U}/hotplug.c ${U}/io.c ${U}/sync.c ${U}/strerror.c
    ${U}/os/linux_usbfs.c ${U}/os/events_posix.c ${U}/os/threads_posix.c ${U}/os/linux_netlink.c)
target_include_directories(usb1 PRIVATE ${libusb_SOURCE_DIR}/android ${U}/os PUBLIC ${U})
target_compile_options(usb1 PRIVATE -pthread -w)
target_link_libraries(usb1 PRIVATE log)

# ── zstd: libuuu нужна только распаковка ──────────────────────────────────────
file(GLOB ZSTD_SRC ${zstd_SOURCE_DIR}/lib/common/*.c ${zstd_SOURCE_DIR}/lib/decompress/*.c)
add_library(zstd STATIC ${ZSTD_SRC})
target_compile_definitions(zstd PRIVATE ZSTD_DISABLE_ASM)
target_include_directories(zstd PUBLIC ${zstd_SOURCE_DIR}/lib)

# ── bzip2 ────────────────────────────────────────────────────────────────────
set(B ${bzip2_SOURCE_DIR})
add_library(bz2 STATIC ${B}/blocksort.c ${B}/huffman.c ${B}/crctable.c ${B}/randtable.c
                       ${B}/compress.c ${B}/decompress.c ${B}/bzlib.c)
target_include_directories(bz2 PUBLIC ${B})
target_compile_options(bz2 PRIVATE -w)

# ── tinyxml2 ─────────────────────────────────────────────────────────────────
add_library(tinyxml2 STATIC ${tinyxml2_SOURCE_DIR}/tinyxml2.cpp)
target_include_directories(tinyxml2 PUBLIC ${tinyxml2_SOURCE_DIR})

# ── libuuu ───────────────────────────────────────────────────────────────────
# Список файлов — из libuuu/CMakeLists.txt. OpenSSL не подключаем: без него
# пропадает только скачивание по https (UUUSSL не определён).
set(L ${mfgtools_SOURCE_DIR}/libuuu)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/gitversion.h "#define GIT_VERSION \"libuuu_1.5.243\"\n")
add_library(uuu STATIC
    ${L}/error.cpp ${L}/buffer.cpp ${L}/cmd.cpp ${L}/config.cpp ${L}/notify.cpp
    ${L}/sdps.cpp ${L}/trans.cpp ${L}/usbhotplug.cpp ${L}/version.cpp ${L}/sdp.cpp
    ${L}/fastboot.cpp ${L}/zip.cpp ${L}/fat.cpp ${L}/tar.cpp ${L}/rominfo.cpp
    ${L}/http.cpp ${L}/hidreport.cpp ${L}/sparse.cpp ${L}/bmap.cpp)
target_include_directories(uuu PUBLIC ${L} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen ${CMAKE_CURRENT_LIST_DIR})
target_compile_options(uuu PRIVATE -Wno-everything)
# Как в upstream: C++14. В C++17 zip.h раскрывает FALLTHROUGH в [[fallthrough]]
# без точки с запятой, и clang (в отличие от gcc) это не пропускает.
set_target_properties(uuu PROPERTIES CXX_STANDARD 14)
target_link_libraries(uuu PUBLIC usb1 zstd bz2 tinyxml2 z)

# Без правки исходников libuuu: в два его файла, где он ищет и открывает
# USB-устройства, принудительно подключается uuu_android_redirect.h. Тот
# подменяет несколько вызовов libusb на наши — устройства приходят из
# UsbManager в виде fd, а не перебором /dev/bus/usb (без root он закрыт).
set_source_files_properties(${L}/usbhotplug.cpp ${L}/trans.cpp PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/uuu_android_redirect.h")
# Без OpenSSL в http.cpp не хватает base64_encode — см. заголовок.
set_source_files_properties(${L}/http.cpp PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/uuu_android_http_fix.h")
# Образ читается прямо из файла, выбранного в проводнике, без копии — см. заголовок.
set_source_files_properties(${L}/buffer.cpp PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/uuu_android_file.h")

# ── Наша библиотека для Kotlin ──────────────────────────────────────────────
add_library(uuuflash SHARED uuu_android_usb.cpp uuu_android_file.cpp uuu_jni.cpp)
target_link_libraries(uuuflash PRIVATE uuu usb1 log)
