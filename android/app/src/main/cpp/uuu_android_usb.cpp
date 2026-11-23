#include "uuu_android_usb.h"

#include <android/log.h>
#include <cstdlib>
#include <mutex>
#include <vector>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "uuu-usb", __VA_ARGS__)

namespace {

struct Entry {
    int fd;
    libusb_device_handle *handle;
    libusb_device *dev;
    int users = 0;         // сколько раз libuuu «открыл» и ещё не «закрыл»
    bool removed = false;  // Kotlin сообщил об отключении
};

// Функции, а не глобальные объекты: libuuu зовёт libusb_init из своего
// статического конструктора, раньше, чем могли бы создаться наши глобальные.
std::mutex &mtx() { static std::mutex m; return m; }
std::vector<Entry> &entries() { static std::vector<Entry> v; return v; }

void release_locked(std::vector<Entry>::iterator it) {
    libusb_close(it->handle);
    LOG("fd=%d освобождён", it->fd);
    entries().erase(it);
}

}  // namespace

int uuu_android_libusb_init(libusb_context **ctx) {
    // Без этого libusb_init перебирает /dev/bus/usb и на Android без root
    // падает. Опция для контекста по умолчанию ставится строго до init.
    libusb_set_option(nullptr, LIBUSB_OPTION_NO_DEVICE_DISCOVERY);
    return libusb_init(ctx);
}

int uuu_android_add_fd(int fd) {
    libusb_device_handle *h = nullptr;
    int r = libusb_wrap_sys_device(nullptr, static_cast<intptr_t>(fd), &h);
    if (r < 0) {
        LOG("libusb_wrap_sys_device(fd=%d): %s", fd, libusb_error_name(r));
        return r;
    }
    libusb_device *d = libusb_get_device(h);
    libusb_device_descriptor desc{};
    libusb_get_device_descriptor(d, &desc);
    LOG("fd=%d добавлен: %04x:%04x", fd, desc.idVendor, desc.idProduct);

    std::lock_guard<std::mutex> lk(mtx());
    entries().push_back(Entry{fd, h, d});
    return 0;
}

bool uuu_android_remove_fd(int fd) {
    std::lock_guard<std::mutex> lk(mtx());
    for (auto it = entries().begin(); it != entries().end(); ++it) {
        if (it->fd != fd) continue;
        if (it->users == 0) {
            release_locked(it);
            return true;
        }
        // libuuu ещё держит handle — закроем, когда отпустит.
        it->removed = true;
        LOG("fd=%d отключён, ждём освобождения (users=%d)", fd, it->users);
        return false;
    }
    return true;
}

ssize_t uuu_android_get_device_list(libusb_device ***list) {
    std::lock_guard<std::mutex> lk(mtx());
    // Формат как у libusb: массив, завершённый nullptr, у каждого устройства
    // +1 ссылка. libuuu освобождает его libusb_free_device_list(list, 1).
    auto **out = static_cast<libusb_device **>(calloc(entries().size() + 1, sizeof(libusb_device *)));
    if (!out) return LIBUSB_ERROR_NO_MEM;
    ssize_t n = 0;
    for (auto &e : entries())
        if (!e.removed) out[n++] = libusb_ref_device(e.dev);
    *list = out;
    return n;
}

int uuu_android_open(libusb_device *dev, libusb_device_handle **handle) {
    std::lock_guard<std::mutex> lk(mtx());
    for (auto &e : entries()) {
        if (e.dev != dev || e.removed) continue;
        e.users++;
        *handle = e.handle;
        return 0;
    }
    *handle = nullptr;
    return LIBUSB_ERROR_NO_DEVICE;
}

void uuu_android_close(libusb_device_handle *handle) {
    if (!handle) return;  // libuuu зовёт close и после неудачного open
    std::lock_guard<std::mutex> lk(mtx());
    for (auto it = entries().begin(); it != entries().end(); ++it) {
        if (it->handle != handle) continue;
        if (it->users > 0) it->users--;
        if (it->removed && it->users == 0) release_locked(it);
        return;
    }
}

int uuu_android_kernel_driver_active(libusb_device_handle *, int) {
    return 0;
}
