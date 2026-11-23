#pragma once
// Список USB-устройств для libuuu на Android.
//
// На ПК libuuu сам перебирает /dev/bus/usb через libusb_get_device_list и
// открывает устройства libusb_open. На Android без root это закрыто: доступ к
// устройству даёт только UsbManager, и только в виде открытого fd. Поэтому
// Kotlin передаёт сюда fd каждого нашего устройства, мы заворачиваем его в
// libusb (libusb_wrap_sys_device) и отдаём libuuu вместо настоящего перебора.
//
// На каждое устройство — ровно один libusb_device_handle на всё время, пока
// оно подключено: libuuu «открывает» и «закрывает» его много раз (чтение
// серийника, каждая команда), а мы лишь считаем пользователей.

#include <libusb.h>
#include <sys/types.h>

// Вызывается из Kotlin.
int  uuu_android_add_fd(int fd);      // 0 или код ошибки libusb
bool uuu_android_remove_fd(int fd);   // true — handle освобождён, fd можно закрывать

// Подменяют вызовы libusb внутри libuuu (см. uuu_android_redirect.h).
int     uuu_android_libusb_init(libusb_context **ctx);
ssize_t uuu_android_get_device_list(libusb_device ***list);
int     uuu_android_open(libusb_device *dev, libusb_device_handle **handle);
void    uuu_android_close(libusb_device_handle *handle);
int     uuu_android_kernel_driver_active(libusb_device_handle *handle, int interface_number);
