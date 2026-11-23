#pragma once
// Принудительно подключается (-include) только в libuuu/buffer.cpp.
//
// libuuu читает образ по пути: stat64() — размер и время, open() + mmap.
// На Android файл, выбранный в проводнике, приходит уже открытым дескриптором,
// а повторно открыть его по пути приложение без особых прав не может. Поэтому
// Kotlin передаёт путь вида "/uuufd/<fd>/<имя>", и для таких путей эти два
// вызова работают с готовым дескриптором (fstat / dup) — без копии 2,6 ГБ.
// Имя в конце пути нужно libuuu: по «.zst» он решает, что распаковывать.
//
// Сначала подключаем всё, что подключает buffer.cpp: в <fstream> и др. есть
// свои open(...), их макрос ниже трогать не должен (дальше include guard).

#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

int uuu_android_stat64(const char *path, struct stat64 *st);
int uuu_android_open(const char *path, int flags, ...);

#define stat64(path, st) uuu_android_stat64(path, st)
#define open(...)        uuu_android_open(__VA_ARGS__)
