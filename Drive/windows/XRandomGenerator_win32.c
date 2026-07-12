/**
 * @file XRandomGenerator_win32.c
 * @brief 随机数生成器Windows平台实现
 * 
 * 使用Windows CryptoAPI (CryptGenRandom) 或 BCrypt 提供密码学安全随机数
 * 对应Qt在Windows上使用 RtlGenRandom 或 BCrypt
 */

#ifdef _WIN32
#include "XRandomGenerator.h"
#include <windows.h>

/* =============== BCrypt 方式（Windows Vista及以上，推荐） =============== */

#ifdef USE_BCRYPT
#include <bcrypt.h>
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
    
    NTSTATUS status = BCryptGenRandom(
        NULL,                           /* 使用默认算法提供者 */
        (PUCHAR)buffer,
        (ULONG)size,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG /* 使用系统首选RNG */
    );
    
    return status == 0; /* STATUS_SUCCESS */
}

#else /* 使用传统CryptoAPI */

#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")

/**
 * @brief 使用CryptoAPI获取安全随机数
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @return 成功返回true
 */
bool XRandomGenerator_platformFillSecure(void* buffer, size_t size) {
    if (!buffer || size == 0) {
        return false;
    }
    
    HCRYPTPROV hProv = 0;
    BOOL result = FALSE;
    
    /* 获取密码学服务提供者句柄 */
    if (CryptAcquireContextW(
            &hProv,
            NULL,                  /* 使用默认密钥容器 */
            NULL,                  /* 使用默认CSP */
            PROV_RSA_FULL,         /* RSA全功能提供者 */
            CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {  /* 无需访问私钥 */
        
        /* 生成随机数 */
        result = CryptGenRandom(hProv, (DWORD)size, (BYTE*)buffer);
        
        /* 释放句柄 */
        CryptReleaseContext(hProv, 0);
    }
    
    return result != FALSE;
}

#endif /* USE_BCRYPT */
#endif /* _WIN32 */
