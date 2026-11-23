#pragma once
// Принудительно подключается (-include) только в libuuu/usbhotplug.cpp и
// libuuu/trans.cpp — см. CMakeLists.txt. Сначала обрабатываем libusb.h, чтобы
// его объявления остались настоящими, и только потом переопределяем имена:
// дальнейший #include <libusb.h> в libuuu уже ничего не делает (include guard).

#include <libusb.h>
#include "uuu_android_usb.h"

// usbhotplug.cpp: инициализация, перебор и открытие устройств.
#define libusb_init(ctx)                 uuu_android_libusb_init(ctx)
#define libusb_get_device_list(ctx, l)   uuu_android_get_device_list(l)
#define libusb_open(dev, h)              uuu_android_open(dev, h)
#define libusb_close(h)                  uuu_android_close(h)

// trans.cpp: драйвер ядра (usbhid у ROM) уже отключил Kotlin через
// claimInterface(force = true) — это проверено на телефоне. Ioctl
// USBDEVFS_GETDRIVER из libusb не трогаем, чтобы не зависеть от SELinux.
#define libusb_kernel_driver_active(h, i) uuu_android_kernel_driver_active(h, i)
