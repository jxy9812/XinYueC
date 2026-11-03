/**
 * @file XLockFileNative_unsupported.c
 * @brief 未提供进程锁后端的平台存根。
 * @details 所有原语返回失败/空结果，保证 XLockFile 公共层在所有平台可链接。
 *          无 OS 的裸机/RTOS 上没有跨进程锁概念，XLockFile 的 lock() 会返回
 *          LockFailedError，调用方应据 isSupported 分支降级。
 */

#include "XLockFileNative.h"

#if XLOCKFILE_ON && !defined(_WIN32) && !defined(__linux__) && !defined(__APPLE__)

#include <string.h>

XLockFileNativeResult XLockFileNative_openExclusive(const uint16_t* path,
                                                    XLockFileNativeHandle* outHandle)
{
    (void)path;
    if (outHandle)
        *outHandle = NULL;
    return XLockFileNativeResult_Failed;
}

void XLockFileNative_closeHandle(XLockFileNativeHandle handle)
{
    (void)handle;
}

bool XLockFileNative_writeContents(XLockFileNativeHandle handle,
                                   const char* data, size_t size)
{
    (void)handle;
    (void)data;
    (void)size;
    return false;
}

bool XLockFileNative_readContents(const uint16_t* path, char* buffer,
                                  size_t capacity, size_t* sizeOut)
{
    (void)path;
    (void)buffer;
    (void)capacity;
    if (sizeOut)
        *sizeOut = 0;
    return false;
}

bool XLockFileNative_removeFile(const uint16_t* path)
{
    (void)path;
    return false;
}

bool XLockFileNative_fileExists(const uint16_t* path)
{
    (void)path;
    return false;
}

bool XLockFileNative_isProcessRunning(int64_t pid)
{
    (void)pid;
    return false;
}

bool XLockFileNative_processNameByPid(int64_t pid, uint16_t* buffer, size_t capacityCodes)
{
    (void)pid;
    if (buffer && capacityCodes > 0)
        buffer[0] = 0;
    return false;
}

int64_t XLockFileNative_currentPid(void)
{
    return 0;
}

bool XLockFileNative_machineName(uint16_t* buffer, size_t capacityCodes)
{
    if (buffer && capacityCodes > 0)
        buffer[0] = 0;
    return false;
}

bool XLockFileNative_machineUniqueId(char* buffer, size_t capacity, size_t* sizeOut)
{
    (void)capacity;
    if (buffer)
        buffer[0] = 0;
    if (sizeOut)
        *sizeOut = 0;
    return false;
}

bool XLockFileNative_bootUniqueId(char* buffer, size_t capacity, size_t* sizeOut)
{
    (void)capacity;
    if (buffer)
        buffer[0] = 0;
    if (sizeOut)
        *sizeOut = 0;
    return false;
}

bool XLockFileNative_fileAgeMsecs(const uint16_t* path, int64_t* msecsOut)
{
    (void)path;
    if (msecsOut)
        *msecsOut = 0;
    return false;
}

void XLockFileNative_sleepMsecs(int64_t msecs)
{
    (void)msecs;
}

#endif /* XLOCKFILE_ON && 非 Windows/POSIX */
