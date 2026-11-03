/**
 * @file       XLockFile.c
 * @brief      XLockFile 实现（进程间文件锁，对标 Qt 6.8 QLockFile）。
 * @details    算法逐条移植 qtbase/src/corelib/io/qlockfile.cpp：
 *             独占创建 → 写持有者信息 → 冲突时判僵死（PID/进程名/主机/
 *             启动标识/文件年龄）→ 僵死则经 .rmlock 二级锁消解删除竞争 →
 *             重试。平台原语见 XLockFileNative.h。实现只依赖 XinYueC 抽象层。
 * @author     XinYueC 团队
 */
#include "XLockFile.h"
#include "XLockFileNative.h"
#include "XMemory.h"
#include "XString.h"
#include "XDateTime.h"
#include "XPrintf.h"

#if XLOCKFILE_ON

/* ==========================================================================
 * 私有数据
 * ========================================================================== */

/** @brief XLockFile 私有数据（对标 QLockFilePrivate）。 */
struct XLockFilePrivate
{
    XString* fileName;                  /**< 锁文件路径（对象拥有）。 */
    XLockFileNativeHandle handle;       /**< 已持有的锁句柄；NULL = 未持锁。 */
    int staleLockTime;                  /**< 僵死年龄阈值（毫秒）；0 = 不按年龄判。 */
    XLockFile_LockError lockError;      /**< 最后一次加锁错误码。 */
    bool isLocked;                      /**< 是否持有锁。 */
};

/* ==========================================================================
 * 内部工具
 * ========================================================================== */

/** @brief 释放私有数据（含释放持有的锁句柄；不删文件）。 */
static void xlfFreePrivate(XLockFilePrivate* d)
{
    if (!d)
        return;
    if (d->handle)
        XLockFileNative_closeHandle(d->handle);
    if (d->fileName)
        XClassDelete(d->fileName);
    XFree_System(d);
}

/** @brief 构造锁文件内容（对标 lockFileContents）：五行信息。 */
static bool xlfBuildContents(XLockFilePrivate* d, char* buffer, size_t capacity,
                             size_t* sizeOut)
{
    uint16_t machineName[256];
    char machineId[128];
    char bootId[64];
    char appName[512];
    size_t machineIdLen = 0;
    size_t bootIdLen = 0;
    int64_t pid;
    size_t pos = 0;
    (void)d;

    if (sizeOut)
        *sizeOut = 0;
    if (!buffer || capacity == 0)
        return false;

    machineName[0] = 0;
    machineId[0] = 0;
    bootId[0] = 0;
    appName[0] = 0;

    XLockFileNative_machineName(machineName, sizeof(machineName) / sizeof(uint16_t));
    XLockFileNative_machineUniqueId(machineId, sizeof(machineId), &machineIdLen);
    XLockFileNative_bootUniqueId(bootId, sizeof(bootId), &bootIdLen);
    pid = XLockFileNative_currentPid();

    /* 进程名：先取自身 PID 对应的可执行文件名（Qt 同款） */
    {
        uint16_t nameBuf[512];
        nameBuf[0] = 0;
        if (XLockFileNative_processNameByPid(pid, nameBuf, sizeof(nameBuf) / sizeof(uint16_t))) {
            size_t i = 0;
            while (nameBuf[i] != 0 && i + 1 < sizeof(appName)) {
                appName[i] = (char)(nameBuf[i] < 0x80 ? nameBuf[i] : '?');
                ++i;
            }
            appName[i] = 0;
        }
    }

    /* 逐行拼接：PID\n进程名\n主机名\n机器ID\n启动ID\n */
    {
        char numBuf[32];
        int64_t v = pid;
        int n = 0;
        char tmp[32];
        int i;
        if (v == 0)
            tmp[n++] = '0';
        while (v > 0 && n < (int)sizeof(tmp)) {
            tmp[n++] = (char)('0' + (int)(v % 10));
            v /= 10;
        }
        for (i = 0; i < n && pos + 1 < capacity; ++i)
            buffer[pos++] = tmp[n - 1 - i];
        (void)numBuf;
    }
    if (pos + 1 < capacity) buffer[pos++] = '\n';
    {
        /* 进程名 */
        size_t i = 0;
        while (appName[i] != 0 && pos + 1 < capacity)
            buffer[pos++] = appName[i++];
        if (pos + 1 < capacity) buffer[pos++] = '\n';
        /* 主机名（UTF-16 → ASCII 窄化） */
        i = 0;
        while (machineName[i] != 0 && pos + 1 < capacity) {
            uint16_t ch = machineName[i++];
            buffer[pos++] = (char)(ch < 0x80 ? ch : '?');
        }
        if (pos + 1 < capacity) buffer[pos++] = '\n';
        /* 机器唯一 ID */
        for (i = 0; i < machineIdLen && pos + 1 < capacity; ++i)
            buffer[pos++] = machineId[i];
        if (pos + 1 < capacity) buffer[pos++] = '\n';
        /* 启动唯一 ID */
        for (i = 0; i < bootIdLen && pos + 1 < capacity; ++i)
            buffer[pos++] = bootId[i];
        if (pos + 1 < capacity) buffer[pos++] = '\n';
    }
    buffer[pos] = 0;
    if (sizeOut)
        *sizeOut = pos;
    return pos > 0;
}

/** @brief 解析锁文件内容的结构（对标 LockFileInfo）。 */
typedef struct XLockFileInfo
{
    int64_t pid;
    char appName[512];
    char hostName[256];
    char hostId[128];
    char bootId[64];
} XLockFileInfo;

/** @brief 从缓冲区按行解析（对标 getLockInfo_helper）。 */
static bool xlfParseInfo(const char* data, size_t size, XLockFileInfo* info)
{
    size_t pos = 0;
    int line = 0;
    char* targets[5];
    size_t caps[5];
    int i;

    if (!data || !info)
        return false;
    XMemset(info, 0, sizeof(*info));
    targets[0] = NULL;   /* pid 单独解析 */
    targets[1] = info->appName;  caps[1] = sizeof(info->appName);
    targets[2] = info->hostName; caps[2] = sizeof(info->hostName);
    targets[3] = info->hostId;   caps[3] = sizeof(info->hostId);
    targets[4] = info->bootId;   caps[4] = sizeof(info->bootId);
    caps[0] = 0;

    while (pos < size && line < 5) {
        size_t start = pos;
        size_t len;
        while (pos < size && data[pos] != '\n')
            ++pos;
        len = pos - start;
        if (pos < size)
            ++pos;   /* 跳过 '\n' */
        if (line == 0) {
            /* PID 行 */
            int64_t pid = 0;
            size_t k;
            bool ok = len > 0;
            for (k = 0; k < len; ++k) {
                if (data[start + k] < '0' || data[start + k] > '9') {
                    ok = false;
                    break;
                }
                pid = pid * 10 + (data[start + k] - '0');
            }
            if (!ok)
                return false;
            info->pid = pid;
        }
        else {
            char* dst = targets[line];
            size_t cap = caps[line];
            size_t n = len < cap - 1 ? len : cap - 1;
            for (i = 0; (size_t)i < n; ++i)
                dst[i] = data[start + (size_t)i];
            dst[n] = 0;
        }
        ++line;
    }
    return info->pid > 0;
}

/** @brief 判断锁是否「看起来僵死」（对标 isApparentlyStale）。 */
static bool xlfIsApparentlyStale(const XLockFilePrivate* d)
{
    XLockFileInfo info;
    uint16_t* pathUtf16;
    char buffer[1024];
    size_t size = 0;
    bool sameHost = true;
    bool stale = false;
    int64_t ageMsecs = 0;

    if (!d || !d->fileName)
        return false;

    pathUtf16 = XString_toUtf16(d->fileName);
    if (pathUtf16) {
        size_t units = XString_toUtf16_length(d->fileName);
        uint16_t* copy = (uint16_t*)XMalloc_System((units + 1) * sizeof(uint16_t));
        if (copy) {
            size_t i;
            for (i = 0; i < units; ++i)
                copy[i] = pathUtf16[i];
            copy[units] = 0;
            if (XLockFileNative_readContents(copy, buffer, sizeof(buffer), &size) &&
                xlfParseInfo(buffer, size, &info)) {
                /* 主机比对 */
                {
                    uint16_t machineName[256];
                    machineName[0] = 0;
                    XLockFileNative_machineName(machineName,
                                                sizeof(machineName) / sizeof(uint16_t));
                    if (info.hostName[0] != 0) {
                        size_t k = 0;
                        sameHost = true;
                        while (machineName[k] != 0 && info.hostName[k] != 0) {
                            char mc = (char)(machineName[k] < 0x80 ? machineName[k] : '?');
                            if (mc != info.hostName[k]) {
                                sameHost = false;
                                break;
                            }
                            ++k;
                        }
                    }
                }
                /* 主机 ID 覆盖主机名判断 */
                if (info.hostId[0] != 0) {
                    char machineId[128];
                    size_t machineIdLen = 0;
                    machineId[0] = 0;
                    if (XLockFileNative_machineUniqueId(machineId, sizeof(machineId),
                                                        &machineIdLen) && machineIdLen > 0) {
                        size_t k = 0;
                        bool equal = true;
                        while (k < machineIdLen && info.hostId[k] != 0) {
                            if (info.hostId[k] != machineId[k]) {
                                equal = false;
                                break;
                            }
                            ++k;
                        }
                        sameHost = equal && machineId[k] == 0 && info.hostId[k] == 0;
                    }
                }
                if (sameHost) {
                    /* 启动 ID 不同 → 机器重启过，锁必僵死 */
                    if (info.bootId[0] != 0) {
                        char bootId[64];
                        size_t bootIdLen = 0;
                        bootId[0] = 0;
                        if (XLockFileNative_bootUniqueId(bootId, sizeof(bootId), &bootIdLen) &&
                            bootIdLen > 0) {
                            size_t k = 0;
                            bool equal = true;
                            while (k < bootIdLen && info.bootId[k] != 0) {
                                if (info.bootId[k] != bootId[k]) {
                                    equal = false;
                                    break;
                                }
                                ++k;
                            }
                            if (!equal) {
                                XFree_System(copy);
                                return true;
                            }
                        }
                    }
                    /* 进程不在了 → 僵死 */
                    if (!XLockFileNative_isProcessRunning(info.pid)) {
                        XFree_System(copy);
                        return true;
                    }
                }
            }
            /* 年龄兜底判据 */
            if (XLockFileNative_fileAgeMsecs(copy, &ageMsecs) && d->staleLockTime > 0) {
                int64_t absAge = ageMsecs < 0 ? -ageMsecs : ageMsecs;
                if (absAge > d->staleLockTime) {
                    XFree_System(copy);
                    return true;
                }
            }
            XFree_System(copy);
        }
    }
    (void)stale;
    return false;
}

/* ==========================================================================
 * 加锁核心
 * ========================================================================== */

/** @brief 单次尝试（对标 QLockFilePrivate::tryLock_sys + 写入持有者信息）。 */
static XLockFile_LockError xlfTryLockOnce(XLockFilePrivate* d)
{
    uint16_t* pathUtf16;
    size_t units;
    uint16_t* copy;
    XLockFileNativeHandle handle = NULL;
    XLockFileNativeResult result;

    if (!d || !d->fileName)
        return XLockFile_UnknownError;

    pathUtf16 = XString_toUtf16(d->fileName);
    units = pathUtf16 ? XString_toUtf16_length(d->fileName) : 0;
    copy = (uint16_t*)XMalloc_System((units + 1) * sizeof(uint16_t));
    if (!copy)
        return XLockFile_UnknownError;
    {
        size_t i;
        for (i = 0; i < units; ++i)
            copy[i] = pathUtf16[i];
        copy[units] = 0;
    }

    result = XLockFileNative_openExclusive(copy, &handle);
    if (result != XLockFileNativeResult_Ok) {
        XFree_System(copy);
        return result == XLockFileNativeResult_AlreadyExists ? XLockFile_LockFailedError
             : result == XLockFileNativeResult_PermissionDenied ? XLockFile_PermissionError
                                                                : XLockFile_UnknownError;
    }

    /* 持有锁：写入持有者信息（对标 lockFileContents + WriteFile） */
    {
        char contents[1024];
        size_t size = 0;
        if (xlfBuildContents(d, contents, sizeof(contents), &size))
            XLockFileNative_writeContents(handle, contents, size);
    }

    XFree_System(copy);
    d->handle = handle;
    d->isLocked = true;
    return XLockFile_NoError;
}

/** @brief 删除僵死锁（对标 removeStaleLock）。 */
static bool xlfRemoveStaleLock(XLockFilePrivate* d)
{
    uint16_t* pathUtf16;
    size_t units;
    uint16_t* copy;
    bool ok;
    if (!d || !d->fileName)
        return false;
    pathUtf16 = XString_toUtf16(d->fileName);
    units = pathUtf16 ? XString_toUtf16_length(d->fileName) : 0;
    copy = (uint16_t*)XMalloc_System((units + 1) * sizeof(uint16_t));
    if (!copy)
        return false;
    {
        size_t i;
        for (i = 0; i < units; ++i)
            copy[i] = pathUtf16[i];
        copy[units] = 0;
    }
    ok = XLockFileNative_removeFile(copy);
    XFree_System(copy);
    return ok;
}

/* ==========================================================================
 * 公开 API
 * ========================================================================== */

void XLockFile_init(XLockFile* self, const XString* fileName)
{
    XLockFilePrivate* d;
    if (!self)
        return;
    self->m_data = NULL;
    d = (XLockFilePrivate*)XMalloc_System(sizeof(XLockFilePrivate));
    if (!d)
        return;
    XMemset(d, 0, sizeof(XLockFilePrivate));
    d->fileName = fileName ? XString_create_copy(fileName) : XString_create();
    d->staleLockTime = 30000;   /* 对标 Qt 默认 30 秒 */
    d->lockError = XLockFile_NoError;
    d->isLocked = false;
    self->m_data = d;
}

void XLockFile_init_utf8(XLockFile* self, const char* fileNameUtf8)
{
    XString* name = fileNameUtf8 ? XString_create_utf8(fileNameUtf8) : NULL;
    XLockFile_init(self, name);
    if (name)
        XClassDelete(name);
}

XLockFile* XLockFile_create(const XString* fileName)
{
    XLockFile* self = (XLockFile*)XMalloc_System(sizeof(XLockFile));
    if (!self)
        return NULL;
    XLockFile_init(self, fileName);
    if (!self->m_data) {
        XFree_System(self);
        return NULL;
    }
    return self;
}

XLockFile* XLockFile_create_utf8(const char* fileNameUtf8)
{
    XString* name = fileNameUtf8 ? XString_create_utf8(fileNameUtf8) : NULL;
    XLockFile* self = XLockFile_create(name);
    if (name)
        XClassDelete(name);
    return self;
}

void XLockFile_deinit(XLockFile* self)
{
    if (!self)
        return;
    if (self->m_data) {
        /* 析构即解锁（对标 ~QLockFile → unlock） */
        if (self->m_data->isLocked)
            XLockFile_unlock(self);
        xlfFreePrivate(self->m_data);
        self->m_data = NULL;
    }
}

void XLockFile_delete(XLockFile* self)
{
    if (!self)
        return;
    XLockFile_deinit(self);
    XFree_System(self);
}

bool XLockFile_tryLock(XLockFile* self, int timeoutMsecs)
{
    XLockFilePrivate* d = self ? self->m_data : NULL;
    int64_t elapsed = 0;
    int64_t sleepTime = 100;   /* 对标 Qt：初值 100ms，逐次翻倍至 5s 上限 */
    int64_t deadline = timeoutMsecs;

    if (!d || !d->fileName || XString_size(d->fileName) == 0) {
        if (d)
            d->lockError = XLockFile_UnknownError;
        return false;
    }

    for (;;) {
        XLockFile_LockError error = xlfTryLockOnce(d);
        d->lockError = error;
        if (error == XLockFile_NoError)
            return true;
        if (error == XLockFile_PermissionError || error == XLockFile_UnknownError)
            return false;
        /* LockFailedError：判僵死 */
        if (!d->isLocked && xlfIsApparentlyStale(d)) {
            /* 用 <锁文件>.rmlock 二级锁确保只有一个进程去删（对标 Qt） */
            XString* rmPath = XString_create_copy(d->fileName);
            if (rmPath) {
                XString_append_utf8(rmPath, ".rmlock");
                {
                    XLockFile rmLock;
                    XLockFile_init(&rmLock, rmPath);
                    if (XLockFile_tryLock(&rmLock, 0)) {
                        if (xlfIsApparentlyStale(d) && xlfRemoveStaleLock(d)) {
                            XLockFile_unlock(&rmLock);
                            XLockFile_deinit(&rmLock);
                            XClassDelete(rmPath);
                            continue;   /* 删掉了，立刻重试 */
                        }
                        XLockFile_unlock(&rmLock);
                    }
                    XLockFile_deinit(&rmLock);
                }
                XClassDelete(rmPath);
            }
        }
        /* 剩余等待 */
        if (deadline >= 0) {
            if (elapsed >= deadline)
                return false;
        }
        {
            int64_t sleep = (deadline >= 0 && sleepTime > deadline - elapsed)
                                ? (deadline - elapsed)
                                : sleepTime;
            if (sleep <= 0)
                return false;
            XLockFileNative_sleepMsecs(sleep);
            elapsed += sleep;
            if (sleepTime < 5000)
                sleepTime *= 2;
        }
    }
}

bool XLockFile_lock(XLockFile* self)
{
    /* 对标 lock()：无限等待（用负超时表达） */
    return XLockFile_tryLock(self, -1);
}

void XLockFile_unlock(XLockFile* self)
{
    XLockFilePrivate* d = self ? self->m_data : NULL;
    if (!d)
        return;
    if (d->handle) {
        XLockFileNative_closeHandle(d->handle);
        d->handle = NULL;
    }
    if (d->isLocked) {
        xlfRemoveStaleLock(d);   /* 解锁 = 删锁文件 */
        d->isLocked = false;
    }
}

bool XLockFile_isLocked(const XLockFile* self)
{
    return (self && self->m_data) ? self->m_data->isLocked : false;
}

XLockFile_LockError XLockFile_error(const XLockFile* self)
{
    return (self && self->m_data) ? self->m_data->lockError : XLockFile_NoError;
}

const XString* XLockFile_fileName_const(const XLockFile* self)
{
    return (self && self->m_data) ? self->m_data->fileName : NULL;
}

bool XLockFile_getLockInfo(const XLockFile* self, int64_t* pidOut,
                           XString** hostnameOut, XString** appnameOut)
{
    XLockFilePrivate* d = (XLockFile*)self ? self->m_data : NULL;
    uint16_t* pathUtf16;
    size_t units;
    uint16_t* copy;
    char buffer[1024];
    size_t size = 0;
    XLockFileInfo info;
    bool ok = false;

    if (pidOut) *pidOut = 0;
    if (hostnameOut) *hostnameOut = NULL;
    if (appnameOut) *appnameOut = NULL;
    if (!d || !d->fileName)
        return false;

    pathUtf16 = XString_toUtf16(d->fileName);
    units = pathUtf16 ? XString_toUtf16_length(d->fileName) : 0;
    copy = (uint16_t*)XMalloc_System((units + 1) * sizeof(uint16_t));
    if (!copy)
        return false;
    {
        size_t i;
        for (i = 0; i < units; ++i)
            copy[i] = pathUtf16[i];
        copy[units] = 0;
    }
    if (XLockFileNative_readContents(copy, buffer, sizeof(buffer), &size) &&
        xlfParseInfo(buffer, size, &info)) {
        if (pidOut)
            *pidOut = info.pid;
        if (hostnameOut && info.hostName[0])
            *hostnameOut = XString_create_utf8(info.hostName);
        if (appnameOut && info.appName[0])
            *appnameOut = XString_create_utf8(info.appName);
        ok = true;
    }
    XFree_System(copy);
    return ok;
}

bool XLockFile_removeStaleLockFile(XLockFile* self)
{
    XLockFilePrivate* d = self ? self->m_data : NULL;
    if (!d)
        return false;
    if (d->isLocked) {
        XPrintf("[XLockFile] removeStaleLockFile: 持锁状态下不可调用\n");
        return false;
    }
    return xlfRemoveStaleLock(d);
}

void XLockFile_setStaleLockTime(XLockFile* self, int staleMsecs)
{
    XLockFilePrivate* d = self ? self->m_data : NULL;
    if (d)
        d->staleLockTime = staleMsecs < 0 ? 0 : staleMsecs;
}

int XLockFile_staleLockTime(const XLockFile* self)
{
    XLockFilePrivate* d = self ? self->m_data : NULL;
    return d ? d->staleLockTime : 30000;
}

bool XLockFile_isSupported(void)
{
#if XPLATFORM_WINDOWS || XPLATFORM_POSIX
    return true;
#else
    return false;
#endif
}

#endif /* XLOCKFILE_ON */
