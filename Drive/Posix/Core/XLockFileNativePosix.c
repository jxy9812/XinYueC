/**
 * @file XLockFileNativePosix.c
 * @brief XLockFile 进程间锁的 POSIX 后端（Linux / macOS）。
 * @details
 * 实现 Src/XCore/XPlatform/XLockFileNative.h 声明的全部原语，逐条对标
 * Qt 6.8.3 qtbase/src/corelib/io/qlockfile_unix.cpp：
 * - openExclusive：open(O_RDWR|O_CREAT|O_EXCL)（不存在才建，已存在返回
 *   LockFailedError），随后 flock(LOCK_EX|LOCK_NB) 独占——Qt 用它保证
 *   「持有锁期间别人删不掉锁文件」；
 * - isProcessRunning：kill(pid, 0) 判存活（ESRCH 即已退出），再比对
 *   可执行文件名以排除 PID 复用（Qt 同款双保险）；
 * - processNameByPid：Linux 走 /proc/<pid>/exe 的 readlink 取 basename
 *   （Qt 同款）；macOS 无 /proc，退化为只判 PID 存活；
 * - machineUniqueId / bootUniqueId：Qt 在 Unix 上取 /etc/machine-id 与
 *   /proc/sys/kernel/random/boot_id，本实现照做。
 *
 * 为什么用 flock 而非 POSIX fcntl 记录锁（Qt qlockfile_unix.cpp 的长注释
 * 同款理由）：fcntl 锁是「进程级」的——同进程内任一 fd 关闭即释放该进程
 * 在该文件上的全部锁，且同进程多线程共享锁，无法表达「线程间互斥」；
 * flock 是「打开文件描述符级」的锁，语义正好对上 Qt 的用法。Android 上
 * flock 不可靠，Qt 改用 fcntl(F_SETLK)，本实现同款分流。
 *
 * 平台头（fcntl.h/sys/file.h/unistd.h 等）只在本文件出现，公共层不感知
 * POSIX。UTF-16 路径在本文件内转 UTF-8 后交给系统调用（POSIX 路径是字节
 * 串，无宽字符 API），转码自包含，不依赖 XString，避免裁剪构建下的额外
 * 依赖与所有权风险。
 *
 * @note 仅 POSIX 构建编译；Windows 由 XLockFileNative_win32.c 提供，
 *       其余平台由 Drive/Unsupported/Core/XLockFileNative_unsupported.c
 *       提供存根。
 */

#include "CXinYueConfig.h"

#if XLOCKFILE_ON && (defined(__linux__) || defined(__APPLE__))

#include "XLockFileNative.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(__linux__)
#include <sys/file.h>
#endif

/* ==========================================================================
 * 内部工具
 * ========================================================================== */

/**
 * @brief UTF-16 → UTF-8 转换（自包含；含代理对 → 四字节序列）。
 * @param text 输入 UTF-16（以 NUL 结尾）。
 * @param out 输出缓冲（调用方提供）。
 * @param capacity 输出容量（字节，含结尾 NUL）。
 * @return 写入字节数（不含结尾 NUL）；空间不足返回 0。
 */
static size_t xlfnUtf16ToUtf8(const uint16_t* text, char* out, size_t capacity)
{
    size_t n = 0;
    size_t i = 0;
    if (!out || capacity == 0)
        return 0;
    out[0] = '\0';
    if (!text)
        return 0;
    while (text[i] != 0) {
        uint32_t cp = text[i];
        unsigned char seq[4];
        size_t seqLen;
        size_t k;
        if (cp >= 0xD800 && cp <= 0xDBFF && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            /* 代理对 → 码点 */
            cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            i += 2;
        }
        else {
            i += 1;
        }
        if (cp < 0x80) {
            seq[0] = (unsigned char)cp;
            seqLen = 1;
        }
        else if (cp < 0x800) {
            seq[0] = (unsigned char)(0xC0 | (cp >> 6));
            seq[1] = (unsigned char)(0x80 | (cp & 0x3F));
            seqLen = 2;
        }
        else if (cp < 0x10000) {
            seq[0] = (unsigned char)(0xE0 | (cp >> 12));
            seq[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            seq[2] = (unsigned char)(0x80 | (cp & 0x3F));
            seqLen = 3;
        }
        else {
            seq[0] = (unsigned char)(0xF0 | (cp >> 18));
            seq[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
            seq[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            seq[3] = (unsigned char)(0x80 | (cp & 0x3F));
            seqLen = 4;
        }
        if (n + seqLen + 1 > capacity)
            return 0;
        for (k = 0; k < seqLen; ++k)
            out[n++] = (char)seq[k];
    }
    out[n] = '\0';
    return n;
}

/**
 * @brief 把 UTF-16 路径转成 UTF-8 堆串（调用方用完释放）。
 * @return 堆串；失败返回 NULL。
 */
static char* xlfnPathToUtf8(const uint16_t* path)
{
    size_t units = 0;
    size_t need;
    char* buf;
    if (!path)
        return NULL;
    while (path[units] != 0)
        ++units;
    /* UTF-8 最坏 4 字节/码元 + 结尾 NUL */
    need = units * 4 + 1;
    buf = (char*)malloc(need);
    if (!buf)
        return NULL;
    if (xlfnUtf16ToUtf8(path, buf, need) == 0 && units > 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

/** @brief ASCII/UTF-8 → UTF-16 窄化写入（宿主名/进程名均为 ASCII 域）。 */
static bool xlfnAsciiToUtf16(const char* text, uint16_t* buffer, size_t capacityCodes)
{
    size_t n = 0;
    if (!text || !buffer || capacityCodes == 0)
        return false;
    while (text[n] != 0) {
        if (n + 1 >= capacityCodes)
            return false;
        buffer[n] = (uint16_t)(unsigned char)text[n];
        ++n;
    }
    buffer[n] = 0;
    return true;
}

/**
 * @brief 施加原生长程锁（对标 Qt 的 setNativeLocks）。
 * @details flock 是「打开文件描述符级」的锁：同一进程内不同 fd 各自独立，
 *          正好表达「线程间也要互斥」；fcntl 记录锁是进程级的（同进程任一
 *          fd 关闭即释放全部锁），无法用于本场景（Qt qlockfile_unix.cpp
 *          同款取舍）。Android 上 flock 不可靠，改用 fcntl(F_SETLK)。
 * @return 加锁成功返回 true。
 */
static bool xlfnSetNativeLocks(int fd)
{
#if defined(__ANDROID__)
    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    if (fcntl(fd, F_SETLK, &fl) == -1)
        return false;
#elif defined(LOCK_EX) && defined(LOCK_NB)
    if (flock(fd, LOCK_EX | LOCK_NB) == -1)
        return false;
#else
    (void)fd;
#endif
    return true;
}

/* ==========================================================================
 * 独占创建 / 关闭 / 写入
 * ========================================================================== */

XLockFileNativeResult XLockFileNative_openExclusive(const uint16_t* path,
                                                    XLockFileNativeHandle* outHandle)
{
    char* pathUtf8;
    int fd;
    if (outHandle)
        *outHandle = NULL;
    if (!path || !outHandle)
        return XLockFileNativeResult_Failed;

    pathUtf8 = xlfnPathToUtf8(path);
    if (!pathUtf8)
        return XLockFileNativeResult_Failed;

    /* O_EXCL 是锁语义的基础：已存在必失败（对标 Qt 的 CREATE_NEW） */
    fd = open(pathUtf8, O_RDWR | O_CREAT | O_EXCL, 0666);
    free(pathUtf8);
    if (fd < 0) {
        switch (errno) {
        case EEXIST:
            return XLockFileNativeResult_AlreadyExists;
        case EACCES:
        case EROFS:
            return XLockFileNativeResult_PermissionDenied;
        default:
            return XLockFileNativeResult_Failed;
        }
    }

    /* 确保持有锁期间别人删不掉锁文件（Qt 同款；失败不致命，仅失去该保护） */
    (void)xlfnSetNativeLocks(fd);

    *outHandle = (XLockFileNativeHandle)(intptr_t)fd;
    return XLockFileNativeResult_Ok;
}

void XLockFileNative_closeHandle(XLockFileNativeHandle handle)
{
    if (!handle)
        return;
    close((int)(intptr_t)handle);
}

bool XLockFileNative_writeContents(XLockFileNativeHandle handle,
                                   const char* data, size_t size)
{
    size_t pos = 0;
    int fd;
    if (!handle || (!data && size > 0))
        return false;
    fd = (int)(intptr_t)handle;
    while (pos < size) {
        ssize_t ret = write(fd, data + pos, size - pos);
        if (ret == -1) {
            if (errno == EINTR)
                continue;
            return false;   /* e.g. 分区写满 */
        }
        if (ret == 0)
            return false;
        pos += (size_t)ret;
    }
    return true;
}

/* ==========================================================================
 * 读取 / 删除 / 存在性
 * ========================================================================== */

bool XLockFileNative_readContents(const uint16_t* path, char* buffer,
                                  size_t capacity, size_t* sizeOut)
{
    char* pathUtf8;
    int fd;
    size_t total = 0;
    if (sizeOut)
        *sizeOut = 0;
    if (!path || !buffer || capacity == 0)
        return false;

    pathUtf8 = xlfnPathToUtf8(path);
    if (!pathUtf8)
        return false;
    fd = open(pathUtf8, O_RDONLY);
    free(pathUtf8);
    if (fd < 0)
        return false;

    while (total + 1 < capacity) {
        ssize_t ret = read(fd, buffer + total, capacity - total - 1);
        if (ret == -1) {
            if (errno == EINTR)
                continue;
            close(fd);
            if (sizeOut)
                *sizeOut = total;
            return total > 0;
        }
        if (ret == 0)
            break;
        total += (size_t)ret;
    }
    close(fd);
    buffer[total] = '\0';
    if (sizeOut)
        *sizeOut = total;
    return true;
}

bool XLockFileNative_removeFile(const uint16_t* path)
{
    char* pathUtf8;
    int rc;
    if (!path)
        return false;
    pathUtf8 = xlfnPathToUtf8(path);
    if (!pathUtf8)
        return false;
    rc = unlink(pathUtf8);
    free(pathUtf8);
    return rc == 0;
}

bool XLockFileNative_fileExists(const uint16_t* path)
{
    char* pathUtf8;
    int rc;
    if (!path)
        return false;
    pathUtf8 = xlfnPathToUtf8(path);
    if (!pathUtf8)
        return false;
    rc = access(pathUtf8, F_OK);
    free(pathUtf8);
    return rc == 0;
}

/* ==========================================================================
 * 进程与主机信息
 * ========================================================================== */

bool XLockFileNative_isProcessRunning(int64_t pid)
{
    if (pid <= 0)
        return false;
    /* kill(pid, 0) 只做存在性检查：0=存活；EPERM=存在但无权限（仍算存活）；
       ESRCH=已退出（Qt 同款判定）。 */
    if (kill((pid_t)pid, 0) == 0)
        return true;
    return errno == EPERM;
}

bool XLockFileNative_processNameByPid(int64_t pid, uint16_t* buffer, size_t capacityCodes)
{
#if defined(__linux__)
    char exePath[64];
    char target[4096];
    ssize_t len;
    const char* base;
    size_t baseLen;
    size_t i;
    size_t k = 0;
    if (pid <= 0 || !buffer || capacityCodes == 0)
        return false;
    buffer[0] = 0;
    snprintf(exePath, sizeof(exePath), "/proc/%lld/exe", (long long)pid);
    len = readlink(exePath, target, sizeof(target) - 1);
    if (len <= 0)
        return false;
    target[len] = '\0';
    /* 去掉内核追加的 " (deleted)" 后缀（进程已删但仍在运行的情形） */
    {
        size_t tlen = strlen(target);
        static const char deleted[] = " (deleted)";
        size_t dlen = sizeof(deleted) - 1;
        if (tlen >= dlen && strcmp(target + tlen - dlen, deleted) == 0)
            target[tlen - dlen] = '\0';
    }
    /* 只取 basename（Qt 的 processNameByPid 同款语义） */
    base = target;
    for (i = 0; target[i] != '\0'; ++i) {
        if (target[i] == '/')
            base = target + i + 1;
    }
    baseLen = strlen(base);
    for (i = 0; i < baseLen && k + 1 < capacityCodes; ++i)
        buffer[k++] = (uint16_t)(unsigned char)base[i];
    buffer[k] = 0;
    return k > 0;
#else
    /* macOS/BSD 无 /proc：取不到可执行名（Qt 在 macOS 用 libproc 的
       proc_name）。此处返回 false——公共层据此退化为「只看 PID 存活」，
       语义降级但不会误判僵死。 */
    if (pid <= 0 || !buffer || capacityCodes == 0)
        return false;
    buffer[0] = 0;
    return false;
#endif
}

int64_t XLockFileNative_currentPid(void)
{
    return (int64_t)getpid();
}

bool XLockFileNative_machineName(uint16_t* buffer, size_t capacityCodes)
{
    char host[256];
    if (!buffer || capacityCodes == 0)
        return false;
    buffer[0] = 0;
    if (gethostname(host, sizeof(host)) != 0)
        return false;
    host[sizeof(host) - 1] = '\0';
    return xlfnAsciiToUtf16(host, buffer, capacityCodes);
}

/**
 * @brief 读一个单行文本文件（machine-id / boot_id 通用读取）。
 */
static bool xlfnReadFirstLine(const char* path, char* buffer, size_t capacity, size_t* sizeOut)
{
    FILE* fp;
    size_t n;
    if (sizeOut)
        *sizeOut = 0;
    if (!path || !buffer || capacity == 0)
        return false;
    fp = fopen(path, "rb");
    if (!fp)
        return false;
    n = fread(buffer, 1, capacity - 1, fp);
    fclose(fp);
    buffer[n] = '\0';
    /* 只取第一行 */
    {
        char* nl = strchr(buffer, '\n');
        if (nl)
            *nl = '\0';
    }
    if (sizeOut)
        *sizeOut = strlen(buffer);
    return n > 0;
}

bool XLockFileNative_machineUniqueId(char* buffer, size_t capacity, size_t* sizeOut)
{
    size_t n = 0;
    if (sizeOut)
        *sizeOut = 0;
    if (!buffer || capacity == 0)
        return false;
    buffer[0] = '\0';
    /* 对标 Qt（Unix）：/etc/machine-id，退化到 dbus 的 machine-id */
    if (!xlfnReadFirstLine("/etc/machine-id", buffer, capacity, &n) || n == 0) {
        if (!xlfnReadFirstLine("/var/lib/dbus/machine-id", buffer, capacity, &n))
            return false;
    }
    if (sizeOut)
        *sizeOut = n;
    return n > 0;
}

bool XLockFileNative_bootUniqueId(char* buffer, size_t capacity, size_t* sizeOut)
{
    size_t n = 0;
    if (sizeOut)
        *sizeOut = 0;
    if (!buffer || capacity == 0)
        return false;
    buffer[0] = '\0';
    /* 对标 Qt（Linux）：/proc/sys/kernel/random/boot_id —— 机器重启即变，
     * 用于判定「锁文件必然已僵死」。非 Linux 无此文件时返回 false。 */
    if (!xlfnReadFirstLine("/proc/sys/kernel/random/boot_id", buffer, capacity, &n))
        return false;
    if (sizeOut)
        *sizeOut = n;
    return n > 0;
}

bool XLockFileNative_fileAgeMsecs(const uint16_t* path, int64_t* msecsOut)
{
    char* pathUtf8;
    struct stat st;
    struct timespec now;
    int64_t ageMs;
    if (msecsOut)
        *msecsOut = 0;
    if (!path || !msecsOut)
        return false;
    pathUtf8 = xlfnPathToUtf8(path);
    if (!pathUtf8)
        return false;
    if (stat(pathUtf8, &st) != 0) {
        free(pathUtf8);
        return false;
    }
    free(pathUtf8);
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return false;
    ageMs = ((int64_t)now.tv_sec - (int64_t)st.st_mtime) * 1000
          + ((int64_t)now.tv_nsec / 1000000) * ((now.tv_sec >= st.st_mtime) ? 1 : -1);
    if (ageMs < 0)
        ageMs = 0;
    *msecsOut = ageMs;
    return true;
}

void XLockFileNative_sleepMsecs(int64_t msecs)
{
    struct timespec ts;
    if (msecs <= 0)
        return;
    ts.tv_sec = (time_t)(msecs / 1000);
    ts.tv_nsec = (long)((msecs % 1000) * 1000000L);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        /* 被信号打断则续睡剩余时间 */
    }
}

#endif /* XLOCKFILE_ON && (defined(__linux__) || defined(__APPLE__)) */
