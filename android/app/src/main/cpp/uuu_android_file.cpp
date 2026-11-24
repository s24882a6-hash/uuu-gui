// Файлы, выбранные в проводнике Android, для libuuu — см. uuu_android_file.h.

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// "/uuufd/<fd>/<имя>" → fd; для любых других путей -1.
//
// После имени ничего быть не должно. libuuu проверяет «<образ>.wic.zst/*»
// сначала как обычный файл (FSFlat — первый в FS_DATA, buffer.cpp): на
// настоящем пути stat() тут падает, и файл уходит распаковщику zstd. Если
// принять такой путь за наш, libuuu запишет на eMMC сжатые байты как есть —
// так и было в 0.6–0.8 (панели с чёрным экраном 23.11).
int fd_of(const char *path) {
    static const char kPrefix[] = "/uuufd/";
    const size_t n = sizeof(kPrefix) - 1;
    if (!path || strncmp(path, kPrefix, n) != 0) return -1;
    char *end = nullptr;
    long fd = strtol(path + n, &end, 10);
    if (end == path + n || *end != '/' || fd < 0) return -1;
    if (end[1] == '\0' || strchr(end + 1, '/') != nullptr) return -1;
    return static_cast<int>(fd);
}

}  // namespace

int uuu_android_stat64(const char *path, struct stat64 *st) {
    int fd = fd_of(path);
    return fd >= 0 ? fstat64(fd, st) : stat64(path, st);
}

int uuu_android_open(const char *path, int flags, ...) {
    int fd = fd_of(path);
    // Копия дескриптора: libuuu после mmap его закроет, исходный держит Kotlin.
    if (fd >= 0) return dup(fd);
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }
    return open(path, flags, mode);
}
