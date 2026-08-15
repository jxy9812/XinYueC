/**
 * @file XRandomGenerator_win32.c
 * @brief 随机数生成器Windows平台实现
 * 
 * 使用 Windows CNG BCrypt 提供密码学安全随机数
 * 对应Qt在Windows上使用 RtlGenRandom 或 BCrypt
 */

#ifdef _WIN32
#include "XRandomGenerator.h"
#include <windows.h>
#include <bcrypt.h>
#include <limits.h>
#pragma comment(lib, "bcrypt.lib")

/**
 * @brief 使用BCrypt获取安全随机数
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @return 成功返回true
 */
bool XRandomGenerator_platformFillSecure(void* buffer, size_t size) {
    if (!buffer || size == 0) {
        return false;
    }
    unsigned char* output = (unsigned char*)buffer;
    while (size > 0) {
        ULONG chunk = size > (size_t)ULONG_MAX ? ULONG_MAX : (ULONG)size;
        NTSTATUS status = BCryptGenRandom(NULL, output, chunk,
                                          BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!BCRYPT_SUCCESS(status)) return false;
        output += chunk;
        size -= chunk;
    }
    return true;
}
#endif /* _WIN32 */
