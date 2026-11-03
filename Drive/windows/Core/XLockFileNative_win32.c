/**
 * @file XLockFileNative_win32.c
 * @brief XLockFile 进程间锁的 Windows 后端。
 * @details
 * 实现 Src/XCore/XPlatform/XLockFileNative.h 声明的全部原语，逐条对标
 * Qt 6.8.3 qtbase/src/corelib/io/qlockfile_win.cpp：
 * - openExclusive：CreateFileW + CREATE_NEW（不存在才建，已存在返回
 *   LockFailedError），共享模式只允许读（对应 Qt 的 FILE_SHARE_READ）；
 * - isProcessRunning：OpenProcess + GetExitCodeProcess 判 STILL_ACTIVE，
 *   再比对可执行文件名以排除 PID 复用（Qt 同款双保险）；
 * - machineUniqueId / bootUniqueId：Qt 在 Windows 上分别取注册表
 *   MachineGuid 与系统启动时间；本实现照做。
 * 平台头（windows.h）只在本文件出现，公共层不感知 Win32。
 * @note 仅 Windows 构建编译；其它平台由 XLockFileNative_unsupported.c 提供存根。
 */

#include "XLockFileNative.h"

#if XLOCKFILE_ON && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "XMemory.h"

/* ==========================================================================
 * 内部工具
 * ========================================================================== */

/** @brief 借用转换：uint16_t* → wchar_t*（Windows 上同宽）。 */
static const wchar_t* xlfnWide(const uint16_t* text)
{
    return (const wchar_t*)text;
}

/** @brief 有界求 UTF-16 序列长度（不含 NUL）。 */
static size_t xlfnLength(const uint16_t* text, size_t limit)
{
    size_t n = 0;
    if (!text)
        return 0;
    while (n < limit && text[n] != 0)
        ++n;
    return n;
}

/** @brief Win32 错误码 → 契约结果（对标 qlockfile_win.cpp 的 switch）。 */
static XLockFileNativeResult xlfnMapError(DWORD error, const wchar_t* path)
{
    switch (error) {
    case ERROR_SHARING_VIOLATION:
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        return XLockFileNativeResult_AlreadyExists;
    case ERROR_ACCESS_DENIED:
        /* 只读文件，或仍被其它进程占用——文件存在时按「被占用」处理
         * （对标 Qt：我们自己不会建只读锁文件） */
        return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES
                   ? XLockFileNativeResult_AlreadyExists
                   : XLockFileNativeResult_PermissionDenied;
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
        return XLockFileNativeResult_PermissionDenied;
    default:
        return XLockFileNativeResult_Failed;
    }
}

/* ==========================================================================
 * 独占创建 / 关闭 / 写入
 * ========================================================================== */

XLockFileNativeResult XLockFileNative_openExclusive(const uint16_t* path,
                                                    XLockFileNativeHandle* outHandle)
{
    SECURITY_ATTRIBUTES securityAtts;
    HANDLE handle;
    if (outHandle)
        *outHandle = NULL;
    if (!path || !outHandle)
        return XLockFileNativeResult_Failed;

    securityAtts.nLength = sizeof(SECURITY_ATTRIBUTES);
    securityAtts.lpSecurityDescriptor = NULL;
    securityAtts.bInheritHandle = FALSE;

    /* 写时允许别人读；不加 FILE_SHARE_DELETE（Windows 上允许强删会制造歧义：
     * 既锁不上，又留不下可读的锁文件）—— 与 Qt 的取舍一致 */
    handle = CreateFileW(xlfnWide(path),
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ,
                         &securityAtts,
                         CREATE_NEW,     /* 已存在则失败：锁语义的基础 */
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return xlfnMapError(GetLastError(), xlfnWide(path));

    *outHandle = (XLockFileNativeHandle)handle;
    return XLockFileNativeResult_Ok;
}

void XLockFileNative_closeHandle(XLockFileNativeHandle handle)
{
    if (handle)
        CloseHandle((HANDLE)handle);
}

bool XLockFileNative_writeContents(XLockFileNativeHandle handle,
                                   const char* data, size_t size)
{
    DWORD written = 0;
    if (!handle || !data)
        return false;
    return WriteFile((HANDLE)handle, data, (DWORD)size, &written, NULL) != 0 &&
           written == (DWORD)size;
}

/* ==========================================================================
 * 读取 / 删除 / 存在性
 * ========================================================================== */

bool XLockFileNative_readContents(const uint16_t* path, char* buffer,
                                  size_t capacity, size_t* sizeOut)
{
    HANDLE handle;
    DWORD read = 0;
    if (sizeOut)
        *sizeOut = 0;
    if (!path || !buffer || capacity == 0)
        return false;

    handle = CreateFileW(xlfnWide(path), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return false;

    if (!ReadFile(handle, buffer, (DWORD)capacity, &read, NULL)) {
        CloseHandle(handle);
        return false;
    }
    CloseHandle(handle);
    if (sizeOut)
        *sizeOut = (size_t)read;
    return true;
}

bool XLockFileNative_removeFile(const uint16_t* path)
{
    if (!path)
        return false;
    /* 文件仍被其它进程持有（未加 FILE_SHARE_DELETE）时这里失败，
     * 正好说明「不是僵死锁」——Qt 依赖同一特性 */
    return DeleteFileW(xlfnWide(path)) != 0;
}

bool XLockFileNative_fileExists(const uint16_t* path)
{
    if (!path)
        return false;
    return GetFileAttributesW(xlfnWide(path)) != INVALID_FILE_ATTRIBUTES;
}

/* ==========================================================================
 * 进程与主机信息
 * ========================================================================== */

bool XLockFileNative_isProcessRunning(int64_t pid)
{
    HANDLE proc;
    DWORD exitCode = 0;
    if (pid <= 0)
        return false;
    proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!proc)
        return false;
    if (!GetExitCodeProcess(proc, &exitCode))
        exitCode = 0;
    CloseHandle(proc);
    return exitCode == STILL_ACTIVE;
}

bool XLockFileNative_processNameByPid(int64_t pid, uint16_t* buffer, size_t capacityCodes)
{
    HANDLE proc;
    DWORD length;
    if (pid <= 0 || !buffer || capacityCodes == 0)
        return false;
    buffer[0] = 0;
    proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!proc)
        return false;
    /* QueryFullProcessImageNameW 免 psapi.lib 链接（GetModuleFileNameExW 需要），
     * 且只需 PROCESS_QUERY_LIMITED_INFORMATION 权限，与上面 OpenProcess 一致 */
    length = (DWORD)capacityCodes;
    if (!QueryFullProcessImageNameW(proc, 0, (wchar_t*)buffer, &length)) {
        CloseHandle(proc);
        return false;
    }
    CloseHandle(proc);
    if (length == 0)
        return false;
    /* 只保留文件名部分（Qt 的 processNameByPid 同样取 basename 语义） */
    {
        size_t len = xlfnLength(buffer, capacityCodes);
        size_t i;
        size_t start = 0;
        for (i = 0; i < len; ++i) {
            if (buffer[i] == (uint16_t)'\\' || buffer[i] == (uint16_t)'/')
                start = i + 1;
        }
        if (start > 0) {
            size_t k = 0;
            for (i = start; i < len; ++i)
                buffer[k++] = buffer[i];
            buffer[k] = 0;
        }
    }
    return true;
}

int64_t XLockFileNative_currentPid(void)
{
    return (int64_t)GetCurrentProcessId();
}

bool XLockFileNative_machineName(uint16_t* buffer, size_t capacityCodes)
{
    DWORD size;
    if (!buffer || capacityCodes == 0)
        return false;
    buffer[0] = 0;
    /* 对标 Qt：用 COMPUTERNAME 环境变量，避免 QSysInfo 那种名称解析开销 */
    size = GetEnvironmentVariableW(L"COMPUTERNAME", (wchar_t*)buffer,
                                   (DWORD)capacityCodes);
    return size > 0 && size < (DWORD)capacityCodes;
}

bool XLockFileNative_machineUniqueId(char* buffer, size_t capacity, size_t* sizeOut)
{
    HKEY key = NULL;
    wchar_t value[256];
    DWORD valueSize = (DWORD)sizeof(value);
    DWORD type = 0;
    if (sizeOut)
        *sizeOut = 0;
    if (!buffer || capacity == 0)
        return false;
    buffer[0] = 0;

    /* 对标 Qt：Windows 上取 HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Cryptography", 0,
                      KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return false;
    if (RegQueryValueExW(key, L"MachineGuid", NULL, &type,
                         (LPBYTE)value, &valueSize) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return false;
    }
    RegCloseKey(key);

    {
        /* UTF-16 → ASCII 窄化（GUID 全是 ASCII 字符） */
        size_t units = (size_t)valueSize / sizeof(wchar_t);
        size_t i;
        size_t n = 0;
        while (units > 0 && value[units - 1] == 0)
            --units;
        for (i = 0; i < units && n + 1 < capacity; ++i) {
            char ch = (char)value[i];
            if (ch == 0)
                break;
            buffer[n++] = ch;
        }
        buffer[n] = 0;
        if (sizeOut)
            *sizeOut = n;
        return n > 0;
    }
}

bool XLockFileNative_bootUniqueId(char* buffer, size_t capacity, size_t* sizeOut)
{
    ULONGLONG uptimeMsecs;
    DWORD ticks;
    int i;
    if (sizeOut)
        *sizeOut = 0;
    if (!buffer || capacity < 24)
        return false;
    buffer[0] = 0;

    /* 用「系统启动时刻」当作 boot id：当前时间 - 开机时长。
     * Qt 在 Windows 上无公开 boot id，其 qlockfile_win.cpp 同样不写该字段；
     * 这里给出一个等价且单调可比的近似值，重启后必然不同。 */
    uptimeMsecs = GetTickCount64();
    ticks = GetTickCount();
    (void)ticks;
    {
        FILETIME nowFt;
        ULARGE_INTEGER now;
        ULONGLONG bootFileTime;
        GetSystemTimeAsFileTime(&nowFt);
        now.LowPart = nowFt.dwLowDateTime;
        now.HighPart = nowFt.dwHighDateTime;
        bootFileTime = now.QuadPart - uptimeMsecs * 10000ULL;   /* 100ns 单位 */
        for (i = 0; i < 16 && (size_t)i + 1 < capacity; ++i) {
            static const char hex[] = "0123456789abcdef";
            buffer[i] = hex[(bootFileTime >> ((15 - i) * 4)) & 0xF];
        }
        buffer[i] = 0;
        if (sizeOut)
            *sizeOut = (size_t)i;
        return i > 0;
    }
}

bool XLockFileNative_fileAgeMsecs(const uint16_t* path, int64_t* msecsOut)
{
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    FILETIME nowFt;
    ULARGE_INTEGER lastWrite;
    ULARGE_INTEGER now;
    if (msecsOut)
        *msecsOut = 0;
    if (!path || !msecsOut)
        return false;
    if (!GetFileAttributesExW(xlfnWide(path), GetFileExInfoStandard, &attrs))
        return false;
    lastWrite.LowPart = attrs.ftLastWriteTime.dwLowDateTime;
    lastWrite.HighPart = attrs.ftLastWriteTime.dwHighDateTime;
    GetSystemTimeAsFileTime(&nowFt);
    now.LowPart = nowFt.dwLowDateTime;
    now.HighPart = nowFt.dwHighDateTime;
    /* FILETIME 为 100ns 单位；有符号相减后换算成毫秒 */
    *msecsOut = (int64_t)((now.QuadPart - lastWrite.QuadPart) / 10000ULL);
    return true;
}

void XLockFileNative_sleepMsecs(int64_t msecs)
{
    if (msecs > 0)
        Sleep((DWORD)msecs);
}

#endif /* XLOCKFILE_ON && defined(_WIN32) */
