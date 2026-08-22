/**
 * @file XRandomGenerator_posix.c
 * @brief 随机数生成器 POSIX 平台实现 (Linux/macOS/BSD)
 */

#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)

#include "XRandomGenerator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

bool XRandomGenerator_platformFillSecure(void* buffer, size_t size) {
    if (!buffer || size == 0) return false;
    static int fd = -1;
    if (fd < 0) {
        fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) return false;
    }
    size_t total = 0;
    while (total < size) {
        ssize_t n = read(fd, (char*)buffer + total, size - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    return total == size;
}

#endif /* POSIX */