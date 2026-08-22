/**
 * @file XDeviceFile_posix.c
 * @brief XDeviceFile POSIX 平台实现（Linux/macOS/BSD，支持 io_uring 异步 I/O）
 *
 * 核心文件 I/O 操作（read/write/copy/flush）通过 io_uring 提交，
 * 使用 io_uring_enter 同步等待完成，实现真正异步 I/O 路径。
 */

#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)

#include "XFileInfo.h"
#include "XFileSystem_config.h"
#if defined(XFILE_USE_PLATFORM_API)

#include "XDeviceFile.h"
#include "XDeviceDir.h"
#include "XStorageInfo.h"
#include "XString.h"
#include "XMemory.h"
#include "XFileDescriptor.h"
#include "XDateTime.h"
#include "XAbstractNetIoRing.h"
#include "XNetIoRingPosix.h"
#include "XObject.h"
#include "XClass.h"
#include "XCoreApplication.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <sys/statvfs.h>
#include <termios.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>

/* 这些函数只服务于 XDeviceFile/XDevice 对旧 XFd 的兼容路径。 */
void XDeviceFile_legacyClose(XFd fd);
int64_t XDeviceFile_legacyRead(XFd fd, void* buffer, int64_t size);
int64_t XDeviceFile_legacyWrite(XFd fd, const void* data, int64_t size);
int64_t XDeviceFile_legacySeek(XFd fd, int64_t offset, XSeekWhence whence);
bool XDeviceFile_legacyFlush(XFd fd);
bool XDeviceFile_legacyResize(XFd fd, int64_t size);
#ifdef __linux__
#include <linux/io_uring.h>
#include <sys/syscall.h>
#endif

/* io_uring 系统调用号 */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif

/* 共享内存段的平台私有句柄（挂在 XFileDescriptor.object 上）。
   段数据由 shmFd（shm_open 返回）承载，信令通道由 signalFd
   （Unix domain 流式套接字）承载，XDeviceFileCommand_Map/Unmap 使用 shmFd。
   信令通道的异步接收完全接入库内部事件通知系统（XAbstractNetIoRing
   全局 io_uring 环，与网络套接字/串口的异步读一致）：
   openSharedMemory 建立信令套接字后即提交常驻异步 RECV，完成事件由
   waitForEvents 处理并写入 m_read.base.result；XDeviceFile_legacyRead 消费
   异步接收缓冲，无数据时通过事件环等待完成通知，不做任何轮询。
   首成员为嵌入式 XObject（事件环分发 CQ 条目时以 desc->object 作为
   事件接收者），关闭时通过 XClass_deinit_base 完整释放。 */
typedef struct XFileSharedMemoryPosix {
    XObject m_object;          /**< 嵌入式 XObject 基类（事件通知接收者，必须为首成员） */
    XEventContext_IO m_read;   /**< 信令通道异步读事件上下文（io_uring RECV） */
    uint8_t m_readBuffer[16];  /**< 异步读缓冲区（存放到达的信令字节） */
    size_t m_rxCount;          /**< 异步接收缓冲内未消费字节数 */
    size_t m_rxOffset;         /**< 异步接收缓冲已消费偏移 */
    bool m_readPending;        /**< 是否有在途异步读（SQE 已提交未完成） */
    int shmFd;                 /**< 命名共享内存段 fd（shm_open 返回） */
    int signalFd;              /**< 信令套接字 fd（Unix domain 流式套接字） */
    bool created;              /**< 是否为创建方（负责在关闭时删除信令套接字路径） */
    char signalPath[128];      /**< 信令套接字路径（关闭时删除） */
} XFileSharedMemoryPosix;

/* 共享内存信令通道异步接收辅助函数（实现在"九、内存映射"小节）。
   XDeviceFile_legacyRead / XDeviceFile_legacyClose 在文件前部使用，需要前置声明。 */
static int64_t xfs_posix_shmRead(XFd fdx, XFileSharedMemoryPosix* m,
                                 void* buf, int64_t len);
static void xfs_posix_shm_cancelRead(XFileSharedMemoryPosix* m);

/* ============================================================================
 * 内部辅助：通过 XFd 获取底层 fd
 * ============================================================================ */

static int XFS_getFd(XFd fdx) {
    XFileDescriptor* desc = XFd_get(fdx);
    if (!desc) return -1;
    return (int)(intptr_t)desc->m_deviceCtx;
}

static bool XFS_writeAll(int fd, const void* buffer, size_t length)
{
    const uint8_t* data = (const uint8_t*)buffer;
    while (length > 0) {
        ssize_t written = write(fd, data, length);
        if (written <= 0) return false;
        data += written;
        length -= (size_t)written;
    }
    return true;
}

static bool XFS_isDirectoryPath(const char* path)
{
    struct stat info;
    return path && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

/* ============================================================================
 * io_uring 辅助：提交一条 I/O SQE 并同步等待完成
 * ============================================================================ */

#ifdef __linux__

/**
 * @brief 通过 io_uring 提交一条 I/O 操作并同步等待完成
 * @param fd      目标文件描述符
 * @param opcode  io_uring 操作码 (IORING_OP_READ / IORING_OP_WRITE / IORING_OP_FSYNC)
 * @param buf     缓冲区
 * @param len     长度
 * @param offset  文件偏移（-1 表示使用当前位置）
 * @return 完成字节数，< 0 表示错误
 */
static int64_t ioUringSyncIO(int fd, uint8_t opcode, void* buf, uint64_t len, int64_t offset) {
    XNetIoRingPosix* ring = (XNetIoRingPosix*)XAbstractNetIoRing_global();
    if (!ring || fd < 0) return -1;

    struct io_uring_sqe* sqe = XNetIoRingPosix_getSqe(ring);
    if (!sqe) return -1;

    /* 使用栈上的 XEventContext 作为完成标识 */
    XEventContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.type = XEventContextType_Type_File;

    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = opcode;
    sqe->fd = fd;
    sqe->addr = (uint64_t)(uintptr_t)buf;
    sqe->len = (unsigned)len;
    if (offset >= 0) sqe->off = (uint64_t)offset;
    sqe->user_data = (uint64_t)(uintptr_t)&ctx;

    XNetIoRingPosix_submitSqe(ring, 1);

    /* 同步等待匹配的完成条目 */
    return (int64_t)XNetIoRingPosix_waitCqe(ring, (uint64_t)(uintptr_t)&ctx);
}

#endif /* __linux__ */

/* ============================================================================
 * 一、核心文件操作
 * ============================================================================ */

XFd XDeviceFile_legacyOpen(const XString* path, int mode, uint32_t openFlags, int* error) {
    if (!path) { if (error) *error = EINVAL; return XFD_INVALID; }
    const char* p = XString_toUtf8(path);
    int flags = 0;
    if (mode & XDeviceFile_ReadOnly) flags |= O_RDONLY;
    if (mode & XDeviceFile_WriteOnly) flags |= (mode & XDeviceFile_ReadOnly) ? O_RDWR : O_WRONLY;
    if (mode & XDeviceFile_Append) flags |= O_APPEND;
    if (mode & XDeviceFile_Truncate) flags |= O_TRUNC;
    if (mode & XDeviceFile_NewOnly) flags |= O_CREAT | O_EXCL;
    if (!(mode & XDeviceFile_NewOnly) && (mode & XDeviceFile_WriteOnly)) flags |= O_CREAT;
    if (openFlags & XDeviceOpenFlag_NonBlocking) flags |= O_NONBLOCK;
    if (openFlags & XDeviceOpenFlag_Exclusive) {
        if (!(flags & O_CREAT)) flags |= O_CREAT;
        flags |= O_EXCL;
    }

    int fd = open(p, flags, 0666);
    if (fd < 0) { if (error) *error = errno; return XFD_INVALID; }
    if (error) *error = 0;
    return XFd_alloc(XFD_TYPE_FILE, (void*)(intptr_t)fd, NULL);
}

XFd XDeviceFile_openStandardInput(int* error)
{
    int fd;
    int flags;
    /* /dev/stdin 产生独立的文件状态标志，避免把 O_NONBLOCK 传播给
       XProcess 启动的子进程；精简 POSIX 系统没有该路径时再退回 dup。 */
    fd = open("/dev/stdin", O_RDONLY | O_NONBLOCK);
    if (fd < 0) fd = dup(STDIN_FILENO);
    if (fd < 0) {
        if (error) *error = errno;
        return XFD_INVALID;
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        close(fd);
        if (error) *error = errno;
        return XFD_INVALID;
    }
    if (error) *error = 0;
    return XFd_alloc(XFD_TYPE_FILE, (void*)(intptr_t)fd, NULL);
}

void XDeviceFile_legacyClose(XFd fdx) {
    XFileDescriptor* desc = XFd_get(fdx);
    if (!desc) return;
    if (desc->m_type == XFD_TYPE_MAPPING) {
        /* 共享内存段：先取消并回收在途异步读（避免悬垂 CQE），再释放
           信令套接字与段 fd，创建方删除信令路径，最后释放嵌入式
           XObject 基类与映射对象。 */
        XFileSharedMemoryPosix* mapping = (XFileSharedMemoryPosix*)desc->object;
        if (mapping) {
            xfs_posix_shm_cancelRead(mapping);
            if (mapping->signalFd >= 0) close(mapping->signalFd);
            if (mapping->shmFd >= 0) close(mapping->shmFd);
            if (mapping->created && mapping->signalPath[0]) unlink(mapping->signalPath);
            /* 移除事件环异步读完成时可能已投递到本对象的未处理事件，
               避免释放后事件队列仍引用本对象。 */
            XCoreApplication_removePostedEvents((XObject*)&mapping->m_object, 0);
            XClass_deinit_base((XClass*)&mapping->m_object);
            XFree_System(mapping);
        }
        XFd_free(fdx);
        return;
    }
    int fd = (int)(intptr_t)desc->m_deviceCtx;
    close(fd);
    XFd_free(fdx);
}

int64_t XDeviceFile_legacySeek(XFd fdx, int64_t offset, XSeekWhence whence) {
    int fd = XFS_getFd(fdx);
    if (fd < 0) return -1;
    int flag;
    switch (whence) {
        case XSeekSet: flag = SEEK_SET; break;
        case XSeekCur: flag = SEEK_CUR; break;
        case XSeekEnd: flag = SEEK_END; break;
        default: return -1;
    }
    return (int64_t)lseek(fd, offset, flag);
}

int64_t XDeviceFile_legacyRead(XFd fdx, void* buf, int64_t len) {
    XFileDescriptor* desc = XFd_get(fdx);
    int fd;
    ssize_t n;
    if (!desc || (len > 0 && !buf) || len < 0) return -1;

    /* 共享内存段的信令通道：走库内部事件通知的异步接收（io_uring 环）。 */
    if (desc->m_type == XFD_TYPE_MAPPING)
        return xfs_posix_shmRead(fdx, (XFileSharedMemoryPosix*)desc->object, buf, len);

    fd = (int)(intptr_t)desc->m_deviceCtx;
    n = read(fd, buf, (size_t)len);
    if (n >= 0) return (int64_t)n;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    return -1;
}

bool XDeviceFile_legacySetStandardInputEcho(XFd fdx, bool enabled)
{
    int fd = XFS_getFd(fdx);
    struct termios attributes;

    /* 只有终端具有回显属性；管道和重定向输入必须保持原行为。 */
    if (fd < 0 || !isatty(fd)) return false;
    if (tcgetattr(fd, &attributes) != 0) return false;
    if (enabled) {
        attributes.c_lflag |= ECHO;
    } else {
        attributes.c_lflag &= (tcflag_t)~ECHO;
    }
    return tcsetattr(fd, TCSANOW, &attributes) == 0;
}

int64_t XDeviceFile_legacyWrite(XFd fdx, const void* buf, int64_t len) {
    XFileDescriptor* desc = XFd_get(fdx);
    int fd;
    ssize_t n;
    if (!desc || (len > 0 && !buf) || len < 0) return -1;

    /* 共享内存段的信令通道：写方向保持同步普通写（信令字节极小，
       且与对端异步读路径互补，无需再提交异步写）。 */
    if (desc->m_type == XFD_TYPE_MAPPING) {
        XFileSharedMemoryPosix* m = (XFileSharedMemoryPosix*)desc->object;
        if (!m || m->signalFd < 0) return -1;
        fd = m->signalFd;
    } else {
        fd = (int)(intptr_t)desc->m_deviceCtx;
    }
    n = write(fd, buf, (size_t)len);
    return (n >= 0) ? (int64_t)n : -1;
}

bool XDeviceFile_legacyFlush(XFd fdx) {
    int fd = XFS_getFd(fdx);
    if (fd < 0) return false;

    return fsync(fd) == 0;
}

bool XDeviceFile_legacyResize(XFd fdx, int64_t size) {
    int fd = XFS_getFd(fdx);
    if (fd < 0) return false;
    return ftruncate(fd, size) == 0;
}

/* ============================================================================
 * 二、文件属性操作
 * ============================================================================ */

/* Unix 权限位 -> Qt XFilePermissions 转换 */
static XFilePermissions unixModeToXPerms(mode_t mode) {
    XFilePermissions p = 0;
    if (mode & S_IRUSR) p |= XFile_ReadOwner | XFile_ReadUser;
    if (mode & S_IWUSR) p |= XFile_WriteOwner | XFile_WriteUser;
    if (mode & S_IXUSR) p |= XFile_ExeOwner | XFile_ExeUser;
    if (mode & S_IRGRP) p |= XFile_ReadGroup;
    if (mode & S_IWGRP) p |= XFile_WriteGroup;
    if (mode & S_IXGRP) p |= XFile_ExeGroup;
    if (mode & S_IROTH) p |= XFile_ReadOther;
    if (mode & S_IWOTH) p |= XFile_WriteOther;
    if (mode & S_IXOTH) p |= XFile_ExeOther;
    return p;
}

/* Qt XFilePermissions -> Unix 权限位 转换 */
static mode_t xPermsToUnixMode(XFilePermissions perms) {
    mode_t mode = 0;
    if (perms & (XFile_ReadOwner | XFile_ReadUser))  mode |= S_IRUSR;
    if (perms & (XFile_WriteOwner | XFile_WriteUser)) mode |= S_IWUSR;
    if (perms & (XFile_ExeOwner | XFile_ExeUser))    mode |= S_IXUSR;
    if (perms & XFile_ReadGroup)  mode |= S_IRGRP;
    if (perms & XFile_WriteGroup) mode |= S_IWGRP;
    if (perms & XFile_ExeGroup)   mode |= S_IXGRP;
    if (perms & XFile_ReadOther)  mode |= S_IROTH;
    if (perms & XFile_WriteOther) mode |= S_IWOTH;
    if (perms & XFile_ExeOther)   mode |= S_IXOTH;
    return mode;
}

static void fillStat(struct stat* st, XFileStat* out) {
    memset(out, 0, sizeof(*out));
    out->exists = true;
    out->size = st->st_size;
    out->metadataChangeTime = st->st_ctime;
    out->modificationTime = st->st_mtime;
    out->accessTime = st->st_atime;
    out->isDir = S_ISDIR(st->st_mode);
    out->isFile = S_ISREG(st->st_mode);
    out->isSymLink = S_ISLNK(st->st_mode);
    out->permissions = unixModeToXPerms(st->st_mode);
#ifdef __APPLE__
    out->birthTime = st->st_birthtime;
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    out->birthTime = st->st_birthtime;
#else
    /* Linux: stx.btime or st_ctime as fallback */
    out->birthTime = st->st_ctime;
#endif
}

bool XDeviceFile_stat(const XString* path, XFileStat* out) {
    if (!path || !out) return false;
    struct stat st;
    if (stat(XString_toUtf8(path), &st) != 0) {
        memset(out, 0, sizeof(*out));
        out->exists = false;
        return false;
    }
    fillStat(&st, out);
    return true;
}

bool XDeviceFile_legacyFstat(XFd fdx, XFileStat* out) {
    int fd = XFS_getFd(fdx);
    if (fd < 0 || !out) return false;
    struct stat st;
    if (fstat(fd, &st) != 0) {
        memset(out, 0, sizeof(*out));
        out->exists = false;
        return false;
    }
    fillStat(&st, out);
    return true;
}

/* ============================================================================
 * 三、文件系统操作
 * ============================================================================ */

/* ============================================================================
 * 回收站 (XDG Trash 规范)
 * ============================================================================ */

static bool ensureTrashDir(const char* trashDir, const char* infoDir)
{
    (void)infoDir;
    if (mkdir(trashDir, 0700) == 0) return true;
    return errno == EEXIST;
}

static bool xfs_moveToTrash(const XString* fileName, XString* pathInTrash) {
    if (!fileName) return false;
    const char* src = XString_toUtf8(fileName);
    if (!src) return false;

    /* 1. 解析 $XDG_DATA_HOME 或 $HOME/.local/share */
    char trashRoot[4096];
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        snprintf(trashRoot, sizeof(trashRoot), "%s/Trash", xdg);
    } else {
        const char* home = getenv("HOME");
        if (!home) {
            struct passwd* pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : NULL;
        }
        if (!home) return XDeviceFile_remove(fileName, XRemoveMode_Permanent, NULL);
        snprintf(trashRoot, sizeof(trashRoot), "%s/.local/share/Trash", home);
    }

    char filesDir[4200];
    char infoDir[4200];
    snprintf(filesDir, sizeof(filesDir), "%s/files", trashRoot);
    snprintf(infoDir, sizeof(infoDir), "%s/info", trashRoot);
    if (!ensureTrashDir(filesDir, infoDir) && errno != EEXIST) return XDeviceFile_remove(fileName, XRemoveMode_Permanent, NULL);
    if (!ensureTrashDir(infoDir, NULL) && errno != EEXIST) return XDeviceFile_remove(fileName, XRemoveMode_Permanent, NULL);

    /* 2. 解析原文件 basename, 必要时按 XDateTime 时间戳去重 */
    const char* base = strrchr(src, '/');
    base = base ? base + 1 : src;

    char dest[4200];
    snprintf(dest, sizeof(dest), "%s/%s", filesDir, base);
    struct stat st;
    if (stat(dest, &st) == 0) {
        XDateTime nowDt = XDateTime_currentDateTime();
        int64_t nowSec = XDateTime_toSecsSinceEpoch(&nowDt);
        snprintf(dest, sizeof(dest), "%s/%s.%lld", filesDir, base, (long long)nowSec);
    }

    /* 3. rename(2) 跨同文件系统时原子; 跨设备时退化为 copy+unlink */
    if (rename(src, dest) == 0) {
        if (pathInTrash) XString_assign_utf8(pathInTrash, dest);
        return true;
    }
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) return XDeviceFile_remove(fileName, XRemoveMode_Permanent, NULL);
    int dfd = open(dest, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (dfd < 0) { close(sfd); return XDeviceFile_remove(fileName, XRemoveMode_Permanent, NULL); }
    char buf[8192]; ssize_t n; bool ok = true;
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        if (!XFS_writeAll(dfd, buf, (size_t)n)) { ok = false; break; }
    }
    if (n < 0) ok = false;
    close(sfd); close(dfd);
    if (!ok) { unlink(dest); return XDeviceFile_remove(fileName, XRemoveMode_Permanent, NULL); }
    if (unlink(src) != 0) { unlink(dest); return false; }
    if (pathInTrash) XString_assign_utf8(pathInTrash, dest);
    return true;
}

bool XDeviceFile_remove(const XString* path, XRemoveMode mode, XString* trashPath) {
    if (!path) return false;
    if (mode == XRemoveMode_Trash) return xfs_moveToTrash(path, trashPath);
    return unlink(XString_toUtf8(path)) == 0;
}

bool XDeviceFile_rename(const XString* oldPath, const XString* newPath) {
    if (!oldPath || !newPath) return false;
    /* Qt 行为: 目标文件已存在时 rename 失败 */
    if (XDeviceFile_exists(newPath)) return false;
    return rename(XString_toUtf8(oldPath), XString_toUtf8(newPath)) == 0;
}

bool XDeviceFile_copy(const XString* srcPath, const XString* dstPath) {
    if (!srcPath || !dstPath) return false;
    /* Qt 行为: 目标文件已存在时 copy 失败 */
    if (XDeviceFile_exists(dstPath)) return false;
    const char* src = XString_toUtf8(srcPath);
    const char* dst = XString_toUtf8(dstPath);
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) return false;
    int dfd = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (dfd < 0) { close(sfd); return false; }

    char buf[8192];
    bool ok = true;

    {
        ssize_t n;
        while ((n = read(sfd, buf, sizeof(buf))) > 0) {
            if (!XFS_writeAll(dfd, buf, (size_t)n)) { ok = false; break; }
        }
        if (n < 0) ok = false;
    }
    close(sfd);
    close(dfd);
    if (!ok) unlink(dst);
    return ok;
}

/* ============================================================================
 * 四、目录操作
 * ============================================================================ */

bool XDeviceFile_mkdir(const XString* path, bool recursive) {
    if (!path) return false;
    const char* p = XString_toUtf8(path);
    if (!p || !p[0]) return false;
    if (recursive) {
        char tmp[PATH_MAX];
        if (strlen(p) >= sizeof(tmp)) return false;
        strcpy(tmp, p);
        tmp[sizeof(tmp) - 1] = '\0';
        for (char* s = tmp + (tmp[0] == '/' ? 1 : 0); *s; s++) {
            if (*s == '/') {
                *s = '\0';
                if (tmp[0] && mkdir(tmp, 0755) != 0 &&
                    (errno != EEXIST || !XFS_isDirectoryPath(tmp))) {
                    *s = '/';
                    return false;
                }
                *s = '/';
            }
        }
    }
    if (mkdir(p, 0755) == 0) return true;
    if (errno == EEXIST) {
        struct stat st;
        return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
    }
    return false;
}

bool XDeviceFile_rmdir(const XString* path, bool recursive) {
    if (!path) return false;
    const char* p = XString_toUtf8(path);
    if (!p || !p[0]) return false;

    if (!recursive) {
        return rmdir(p) == 0;
    }

    /* 递归删除：遍历目录内容，先删除子项，再删除目录 */
    DIR* d = opendir(p);
    if (!d) return false;

    struct dirent* de;
    bool ok = true;
    char fullPath[PATH_MAX];

    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        int n = snprintf(fullPath, sizeof(fullPath), "%s/%s", p, de->d_name);
        if (n < 0 || (size_t)n >= sizeof(fullPath)) {
            ok = false;
            continue;
        }

        struct stat st;
        if (lstat(fullPath, &st) == 0 && S_ISDIR(st.st_mode)) {
            XString* subPath = XString_create_utf8(fullPath);
            if (subPath) {
                if (!XDeviceFile_rmdir(subPath, true)) ok = false;
                XString_delete_base(subPath);
            } else ok = false;
        } else {
            if (unlink(fullPath) != 0) ok = false;
        }
    }
    closedir(d);
    return ok && rmdir(p) == 0;
}

typedef struct { DIR* dir; } PosixDirIter;

void* XDeviceDir_platformOpen(const XString* path) {
    if (!path) return NULL;
    DIR* d = opendir(XString_toUtf8(path));
    if (!d) return NULL;
    PosixDirIter* iter = (PosixDirIter*)XMalloc_System(sizeof(PosixDirIter));
    if (!iter) { closedir(d); return NULL; }
    iter->dir = d;
    return (void*)iter;
}

bool XDeviceDir_platformRead(void* backendHandle, XDirEntry* entry) {
    if (!backendHandle || !entry || !entry->name) return false;
    PosixDirIter* it = (PosixDirIter*)backendHandle;
    struct dirent* de = readdir(it->dir);
    if (!de) return false;
    unsigned char type = de->d_type;
    if (type == DT_UNKNOWN) {
        struct stat st;
        if (fstatat(dirfd(it->dir), de->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0) {
            if (S_ISDIR(st.st_mode)) type = DT_DIR;
            else if (S_ISREG(st.st_mode)) type = DT_REG;
            else if (S_ISLNK(st.st_mode)) type = DT_LNK;
        }
    }
    XString_assign_utf8(entry->name, de->d_name);
    entry->isDir = (type == DT_DIR);
    entry->isFile = (type == DT_REG);
    entry->isSymLink = (type == DT_LNK);
    entry->isHidden = (de->d_name[0] == '.');
    return true;
}

void XDeviceDir_platformClose(void* backendHandle) {
    if (!backendHandle) return;
    PosixDirIter* it = (PosixDirIter*)backendHandle;
    closedir(it->dir);
    XFree_System(it);
}

/* ============================================================================
 * 五、路径操作
 * ============================================================================ */

/* 绝对路径仅清理 .、.. 和重复分隔符，保留符号链接文本以维持操作对象语义。 */
static bool XFS_normalizeAbsolutePath(const char* path, char* result, size_t capacity)
{
    char absolute[PATH_MAX];
    const char* source;
    size_t length;
    size_t output = 1;

    if (!path || !result || capacity == 0) return false;
    if (path[0] == '/') {
        length = strlen(path);
        if (length >= sizeof(absolute)) return false;
        memcpy(absolute, path, length + 1);
    } else {
        if (!getcwd(absolute, sizeof(absolute))) return false;
        length = strlen(absolute);
        if (length + 1 >= sizeof(absolute)) return false;
        absolute[length++] = '/';
        if (strlen(path) >= sizeof(absolute) - length) return false;
        strcpy(absolute + length, path);
    }

    result[0] = '/';
    result[1] = '\0';
    source = absolute;
    while (*source) {
        const char* begin;
        const char* end;
        size_t partLength;

        while (*source == '/') source++;
        if (!*source) break;
        begin = source;
        while (*source && *source != '/') source++;
        end = source;
        partLength = (size_t)(end - begin);
        if (partLength == 1 && begin[0] == '.') continue;
        if (partLength == 2 && begin[0] == '.' && begin[1] == '.') {
            if (output > 1) {
                output--;
                while (output > 0 && result[output - 1] != '/') output--;
                result[output] = '\0';
            }
            continue;
        }
        if (output > 1) {
            if (output + 1 + partLength >= capacity) return false;
            result[output++] = '/';
        } else if (output + partLength >= capacity) {
            return false;
        }
        memcpy(result + output, begin, partLength);
        output += partLength;
        result[output] = '\0';
    }
    return true;
}

bool XDeviceFile_resolvePath(const XString* path, XString* result, XPathStyle style) {
    const char* value;
    if (!path || !result) return false;
    value = XString_toUtf8(path);
    if (!value) return false;
    if (style == XPathStyle_Canonical) {
        char canonical[PATH_MAX];
        if (!realpath(value, canonical)) return false;
        XString_assign_utf8(result, canonical);
        return true;
    }
    {
        char absolute[PATH_MAX];
        if (!XFS_normalizeAbsolutePath(value, absolute, sizeof(absolute))) return false;
        XString_assign_utf8(result, absolute);
    }
    return true;
}

/* ============================================================================
 * 六、特殊路径
 * ============================================================================ */

bool XDeviceFile_getSpecialPath(XSpecialPath type, XString* path) {
    if (!path) return false;
    switch (type) {
    case XSpecialPath_Current: {
        char buf[PATH_MAX];
        if (!getcwd(buf, sizeof(buf))) return false;
        XString_assign_utf8(path, buf);
        return true;
    }
    case XSpecialPath_Home: {
        const char* home = getenv("HOME");
        if (!home) {
            struct passwd* pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/";
        }
        XString_assign_utf8(path, home);
        return true;
    }
    case XSpecialPath_Root: {
        XString_assign_utf8(path, "/");
        return true;
    }
    case XSpecialPath_Temp: {
        const char* tmp = getenv("TMPDIR");
        if (!tmp) tmp = getenv("TMP");
        if (!tmp) tmp = "/tmp";
        XString_assign_utf8(path, tmp);
        return true;
    }
    default:
        return false;
    }
}

bool XDeviceFile_setCurrentPath(const XString* path) {
    if (!path) return false;
    return chdir(XString_toUtf8(path)) == 0;
}

/* ============================================================================
 * 七、符号链接操作
 * ============================================================================ */

bool XDeviceFile_link(const XString* targetPath, const XString* linkPath, XLinkType type) {
    if (!targetPath || !linkPath) return false;
    if (type == XLinkType_Hard) return link(XString_toUtf8(targetPath), XString_toUtf8(linkPath)) == 0;
    return symlink(XString_toUtf8(targetPath), XString_toUtf8(linkPath)) == 0;
}

bool XDeviceFile_readLink(const XString* path, XString* target) {
    if (!path || !target) return false;
    char buf[1024];
    ssize_t n = readlink(XString_toUtf8(path), buf, sizeof(buf) - 1);
    if (n < 0) return false;
    buf[n] = '\0';
    XString_assign_utf8(target, buf);
    return true;
}

/* ============================================================================
 * 八、权限操作
 * ============================================================================ */

bool XDeviceFile_setPermissions(const XString* path, XFilePermissions permissions) {
    if (!path) return false;
    return chmod(XString_toUtf8(path), xPermsToUnixMode(permissions)) == 0;
}

/* ============================================================================
 * 九、内存映射（3个）- 可选
 * ============================================================================
 *
 * 共享内存段在 POSIX 上由两块组成：
 *   1. 命名共享内存段（shm_open）：存放跨进程数据，通过 XDeviceFileCommand_Map 映射；
 *   2. 命名信令通道（Unix domain 流式套接字，路径 <共享内存名>.sig）：
 *      数据方写完一块数据后向通道写入 1 个信令字节，对端通过库内部
 *      事件通知系统（XAbstractNetIoRing 全局 io_uring 环）异步接收，
 *      与网络套接字/串口的异步读完全一致，无需轮询共享内存状态字段。
 *      该通道内建于平台实现，不新增任何公共 API。
 *
 * XFd 句柄为信令套接字 fd，object 保存共享内存段 fd、路径信息与异步读
 * 事件上下文；XDeviceFile_legacyRead / XDeviceFile_legacyWrite 在信令通道上收发
 * 通知字节，XDeviceFileCommand_Map / XDeviceFile_legacyClose 据此完成映射与释放。
 */

/* 信令通道默认目录（无 P_tmpdir 时回退 /tmp）。 */
#ifndef XFILE_SHM_SIGNAL_DIR
#define XFILE_SHM_SIGNAL_DIR "/tmp"
#endif

/* 信令通道异步读的有界等待片（毫秒）：XDeviceFile_legacyRead 在无信令字节时
   通过事件环的 waitForEvents 最多阻塞该时长后返回 0，传输层据此检查
   整体超时；信令到达时事件环立即唤醒，不存在任何轮询。 */
#define XFILE_SHM_SIGNAL_RCVTIMEO_MS 500

/* 打开已有段时 connect 的重试次数与间隔：
   服务端创建数据段先 bind/listen 再 accept，客户端可能在服务端 bind 前
   发起 connect（ENOENT/ECONNREFUSED），按套接字惯例重试有限次数。 */
#define XFILE_SHM_SIGNAL_CONNECT_RETRY 100
#define XFILE_SHM_SIGNAL_CONNECT_WAIT_MS 20

/* 异步读"尚未完成"的哨兵值：m_read.base.result 等于该值表示 SQE 已提交
   但完成事件尚未被事件环处理；完成后 result 为实际传输结果
   （>=0 字节数，<0 为负 errno）。 */
#define XFS_SHM_READ_PENDING INT64_MIN

/* 关闭时取消异步读后、等待完成事件回收的最多轮询片数
   （每片 XFILE_SHM_SIGNAL_RCVTIMEO_MS，用于防御平台异常）。 */
#define XFS_SHM_CANCEL_RECLAIM_TRIES 50

/* 将共享内存段名称整理为合法的信令套接字路径：
   仅保留字母数字与 ._-，其余字符替换为 '_'，避免 shm 名称中的 '/' 等
   字符破坏文件系统路径。失败（名称超长等）返回 false。 */
static bool xfs_posix_makeSignalPath(const char* name, char* out, size_t outSize)
{
    size_t i, n;
    if (!name || !out || outSize == 0) return false;
    n = strlen(name);
    if (n > outSize - 64) return false; /* 预留目录与后缀空间 */
    if (snprintf(out, outSize, "%s/", XFILE_SHM_SIGNAL_DIR) >= (int)outSize) return false;
    {
        size_t used = strlen(out);
        for (i = 0; i < n && used + 1 < outSize - 4; ++i) {
            char c = name[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
                out[used++] = c;
            else
                out[used++] = '_';
        }
        out[used] = '\0';
    }
    strncat(out, ".sig", outSize - strlen(out) - 1);
    return true;
}

/* 创建信令套接字（服务端）：bind + listen 后阻塞 accept，等待对端连接，
   与网络套接字服务端 accept 语义一致（create=true 会一直阻塞到对端
   openSharedMemory(create=false) 连接成功，取消亦然）。成功返回信号 fd，
   失败返回 -1。已建立连接不设置接收超时：读取路径统一走 io_uring 事件环
   的异步接收。 */
static int xfs_posix_signalListen(const char* path)
{
    int fd;
    struct sockaddr_un addr;
    int opt = 1;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    /* 清理上一次运行遗留的同名路径（不存在时忽略）。 */
    unlink(path);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) { close(fd); return -1; }
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) { close(fd); return -1; }
    if (listen(fd, 1) != 0) { close(fd); return -1; }

    {
        int accepted = accept(fd, NULL, NULL);
        close(fd);
        if (accepted < 0) return -1;
        return accepted;
    }
}

/* 连接信令套接字（客户端）：服务端可能在绑定的瞬间尚未就绪，
   按网络套接字惯例对 ENOENT/ECONNREFUSED 做有限重试。 */
static int xfs_posix_signalConnect(const char* path)
{
    int attempt;
    struct sockaddr_un addr;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) return -1;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    for (attempt = 0; attempt < XFILE_SHM_SIGNAL_CONNECT_RETRY; ++attempt) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0)
            return fd;
        close(fd);
        if (errno != ENOENT && errno != ECONNREFUSED)
            return -1;
        usleep(XFILE_SHM_SIGNAL_CONNECT_WAIT_MS * 1000);
    }
    return -1;
}

/* ============================================================================
 * 信令通道异步接收（库内部事件通知）
 *
 * 读取路径与网络套接字/串口完全一致：openSharedMemory 建立信令套接字
 * 后即提交常驻异步 RECV（IORING_OP_RECV，提交到 XAbstractNetIoRing
 * 全局 io_uring 环），完成事件由事件环的 waitForEvents（内核 poll
 * ring fd）处理并写入 m_read.base.result；XDeviceFile_legacyRead 消费
 * m_readBuffer 中的字节，无数据时通过事件环有界等待完成通知（超时片
 * 返回 0 供传输层检查整体截止时间），整个过程不轮询共享内存状态字段。
 * ============================================================================ */

/* 提交信令通道异步读（io_uring IORING_OP_RECV）。
   读入 mapping->m_readBuffer，完成事件由事件环处理并写入
   mapping->m_read.base.result。成功返回 true；无事件环或提交失败
   返回 false。调用方保证当前无在途读。 */
static bool xfs_posix_shm_armRead(XFd fdx, XFileSharedMemoryPosix* m)
{
#ifdef __linux__
    XAbstractNetIoRing* ring;
    struct io_uring_sqe* sqe;

    if (!m || m->m_readPending || m->signalFd < 0) return false;
    ring = XAbstractNetIoRing_global();
    if (!ring) return false;
    sqe = XNetIoRingPosix_getSqe((XNetIoRingPosix*)ring);
    if (!sqe) return false;

    memset(&m->m_read, 0, sizeof(m->m_read));
    m->m_read.base.type = XEventContextType_Type_File;
    m->m_read.base.fd = fdx;
    m->m_read.base.opcode = IORING_OP_RECV;
    m->m_read.base.eventMask = XSocketAct_Read;
    m->m_read.base.buffer = m->m_readBuffer;
    m->m_read.base.bufferSize = sizeof(m->m_readBuffer);
    m->m_read.base.result = XFS_SHM_READ_PENDING; /* 哨兵：尚未完成 */
    m->m_read.socket = XSocketDescriptor_fromIntptr(m->signalFd);

    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_RECV;
    sqe->fd = m->signalFd;
    sqe->addr = (uint64_t)(uintptr_t)m->m_readBuffer;
    sqe->len = (uint32_t)sizeof(m->m_readBuffer);
    sqe->user_data = (uint64_t)(uintptr_t)&m->m_read.base;

    XNetIoRingPosix_submitSqe((XNetIoRingPosix*)ring, 1);
    m->m_readPending = true;
    return true;
#else
    (void)fdx;
    (void)m;
    return false;
#endif
}

/* 通过库内部事件环等待信令通道异步读完成（有界等待）：
   先结算事件环可能已批处理的本读完成事件；若尚未完成，则以
   XFILE_SHM_SIGNAL_RCVTIMEO_MS 为总截止时间循环调用事件环
   waitForEvents（内核 poll ring fd，信令到达立即唤醒）。
   注意 waitForEvents 可能因其他事件源（wakeUp 唤醒、其他上下文
   的 CQ 完成）提前返回，本函数只认本读上下文结果的变化，不把无关
   唤醒误判为超时；总等待片到点仍未完成才返回 false（由调用方按
   整体截止时间处理）。完成后 m_read.base.result 携带实际结果。
   返回 true 表示本读已完成，false 表示等待片内未完成。 */
static bool xfs_posix_shm_waitReadable(XFileSharedMemoryPosix* m)
{
    XAbstractNetIoRing* ring;
    int64_t deadline;
    int64_t now;

    if (!m || !m->m_readPending) return false;
    if (m->m_read.base.result != XFS_SHM_READ_PENDING)
        return true; /* 事件环已批处理完成本读 */
    ring = XAbstractNetIoRing_global();
    if (!ring) return false;

    /* 以本等待片为总截止时间，循环等待本读的完成事件；
       waitForEvents 每次按剩余时间有界等待，避免无关唤醒导致忙转。 */
    deadline = XDateTime_currentMSecsSinceEpoch() + XFILE_SHM_SIGNAL_RCVTIMEO_MS;
    for (;;) {
        int waitMs;
        now = XDateTime_currentMSecsSinceEpoch();
        if (now >= deadline) break;
        waitMs = (int)(deadline - now);
        if (waitMs <= 0) break;
        XAbstractNetIoRing_waitForEvents_base(ring, waitMs);
        if (m->m_read.base.result != XFS_SHM_READ_PENDING)
            return true;
    }
    return false;
}

/* 取消在途异步读并回收完成事件（XDeviceFile_legacyClose 关闭前调用）：
   提交 IORING_OP_ASYNC_CANCEL 后通过事件环有界等待被取消请求的 CQE
   到达（result 被写入 -ECANCELED 或实际结果），确保释放 mapping 前
   不存在悬垂 CQE 指向已释放的读上下文。 */
static void xfs_posix_shm_cancelRead(XFileSharedMemoryPosix* m)
{
#ifdef __linux__
    XAbstractNetIoRing* ring;
    struct io_uring_sqe* sqe;
    int tries = 0;

    if (!m || !m->m_readPending) return;
    ring = XAbstractNetIoRing_global();
    if (!ring) { m->m_readPending = false; return; }
    sqe = XNetIoRingPosix_getSqe((XNetIoRingPosix*)ring);
    if (!sqe) { m->m_readPending = false; return; }

    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_ASYNC_CANCEL;
    sqe->addr = (uint64_t)(uintptr_t)&m->m_read.base;
    sqe->user_data = 0; /* 取消包本身不携带事件上下文，其 CQE 直接忽略 */
    XNetIoRingPosix_submitSqe((XNetIoRingPosix*)ring, 1);

    while (m->m_read.base.result == XFS_SHM_READ_PENDING
           && tries < XFS_SHM_CANCEL_RECLAIM_TRIES) {
        XAbstractNetIoRing_waitForEvents_base(ring, XFILE_SHM_SIGNAL_RCVTIMEO_MS);
        ++tries;
    }
    m->m_readPending = false;
    m->m_rxCount = 0;
    m->m_rxOffset = 0;
#else
    (void)m;
#endif
}

/* XDeviceFile_legacyRead 的共享内存信令通道实现：
   优先消费异步接收缓冲；无数据时通过库内部事件环等待完成通知，
   超时片返回 0（调用方检查整体截止时间），对端关闭/通道错误返回 -1。 */
static int64_t xfs_posix_shmRead(XFd fdx, XFileSharedMemoryPosix* m,
                                 void* buf, int64_t len)
{
    size_t n;

    if (!m || m->signalFd < 0) return -1;
    if (len <= 0) return 0;

    /* 1. 优先消费异步接收缓冲中的字节；缓冲耗尽后立即重新武装下一次
          异步读，保证信令通道始终有常驻接收请求（与网络套接字一致）。 */
    if (m->m_rxCount > 0) {
        n = ((size_t)len < m->m_rxCount) ? (size_t)len : m->m_rxCount;
        memcpy(buf, m->m_readBuffer + m->m_rxOffset, n);
        m->m_rxOffset += n;
        m->m_rxCount -= n;
        if (m->m_rxCount == 0)
            (void)xfs_posix_shm_armRead(fdx, m);
        return (int64_t)n;
    }

    /* 2. 无事件环（未创建事件调度器）的退化路径：直接在内核阻塞读，
          不轮询、不设接收超时；正常使用（XCoreApplication 事件循环）
          下不会走到这里。 */
    if (!XAbstractNetIoRing_global()) {
        ssize_t r = read(m->signalFd, buf, (size_t)len);
        return (r >= 0) ? (int64_t)r : -1;
    }

    /* 3. 确保在途异步读后，通过事件环等待完成通知（不忙轮询）。 */
    if (!m->m_readPending) {
        if (!xfs_posix_shm_armRead(fdx, m)) return -1;
    }
    if (!xfs_posix_shm_waitReadable(m))
        return 0; /* 超时片：由调用方检查整体截止时间 */

    m->m_readPending = false;
    {
        int64_t res = m->m_read.base.result;
        m->m_read.base.result = XFS_SHM_READ_PENDING; /* 复位哨兵 */
        if (res <= 0) {
            /* 0=对端关闭（EOF），负=通道错误：均视为通道不可用。 */
            m->m_rxCount = 0;
            m->m_rxOffset = 0;
            return -1;
        }
        m->m_rxCount = (size_t)res;
        m->m_rxOffset = 0;
        n = ((size_t)len < m->m_rxCount) ? (size_t)len : m->m_rxCount;
        memcpy(buf, m->m_readBuffer, n);
        m->m_rxOffset = n;
        m->m_rxCount -= n;
        if (m->m_rxCount == 0)
            (void)xfs_posix_shm_armRead(fdx, m);
        return (int64_t)n;
    }
}

XFd XDeviceFile_openSharedMemory(const XString* name, bool create, int64_t maxSize, int* error)
{
    const char* n;
    int shmFd = -1;
    int signalFd = -1;
    char sigPath[128];
    XFileSharedMemoryPosix* mapping = NULL;
    XFd result = XFD_INVALID;

    if (error) *error = 0;
    if (!name || (create && maxSize <= 0)) {
        if (error) *error = EINVAL;
        return XFD_INVALID;
    }
    n = XString_toUtf8(name);
    if (!n) {
        if (error) *error = EINVAL;
        return XFD_INVALID;
    }

    shmFd = shm_open(n, O_RDWR | (create ? O_CREAT : 0), 0600);
    if (shmFd < 0) {
        if (error) *error = errno;
        goto fail;
    }
    if (create && ftruncate(shmFd, (off_t)maxSize) != 0) {
        if (error) *error = errno;
        goto fail;
    }

    if (!xfs_posix_makeSignalPath(n, sigPath, sizeof(sigPath))) {
        if (error) *error = EINVAL;
        goto fail;
    }
    signalFd = create ? xfs_posix_signalListen(sigPath)
                      : xfs_posix_signalConnect(sigPath);
    if (signalFd < 0) {
        if (error) *error = errno;
        goto fail;
    }

    mapping = (XFileSharedMemoryPosix*)XCalloc_System(1, sizeof(XFileSharedMemoryPosix));
    if (!mapping) {
        if (error) *error = ENOMEM;
        goto fail;
    }
    XObject_init(&mapping->m_object);
    mapping->shmFd = shmFd;
    mapping->signalFd = signalFd;
    mapping->created = create;
    strncpy(mapping->signalPath, sigPath, sizeof(mapping->signalPath) - 1);
    mapping->signalPath[sizeof(mapping->signalPath) - 1] = '\0';

    result = XFd_alloc(XFD_TYPE_MAPPING, (void*)(intptr_t)signalFd, mapping);
    if (result < 0) {
        if (error) *error = EMFILE;
        goto fail;
    }

    /* 建立信令套接字后立即开启异步接收（库内部事件通知，与网络
       套接字 open 后 startAsyncRead 语义一致）。 */
    (void)xfs_posix_shm_armRead(result, mapping);

    /* n 为 XString_toUtf8 返回的借用指针（XString 内部缓存），
       由 name 所有者持有，调用方不得释放。 */
    return result;

fail:
    if (mapping) {
        if (mapping->signalFd >= 0) close(mapping->signalFd);
        if (mapping->created && mapping->signalPath[0]) unlink(mapping->signalPath);
        XClass_deinit_base((XClass*)&mapping->m_object);
        XFree_System(mapping);
    } else {
        if (signalFd >= 0) { close(signalFd); if (create && sigPath[0]) unlink(sigPath); }
    }
    if (shmFd >= 0) close(shmFd);
    /* n 为借用指针（XString 内部 UTF-8 缓存），不得在此释放，
       否则 name 析构时会对同一块缓存二次释放。 */
    return XFD_INVALID;
}

/* Linux mmap 要求 offset 与 size 都按页大小对齐.
   对非页对齐的 offset/size, 我们做向下对齐 offset 并扩张 size, 然后返回偏移后的用户指针. */
#include <unistd.h>
#define XFILE_PAGE_SIZE 4096
#define XFILE_PAGE_MASK (XFILE_PAGE_SIZE - 1)

void* XDeviceFile_legacyMap(XFd fdx, int64_t offset, int64_t size, int flags) {
    XFileDescriptor* desc = XFd_get(fdx);
    int fd;
    if (!desc) return NULL;
    if (desc->m_type == XFD_TYPE_MAPPING) {
        /* 共享内存段：句柄是信令套接字，真正的段 fd 保存在 object。 */
        XFileSharedMemoryPosix* mapping = (XFileSharedMemoryPosix*)desc->object;
        if (!mapping || mapping->shmFd < 0) return NULL;
        fd = mapping->shmFd;
    } else {
        fd = (int)(intptr_t)desc->m_deviceCtx;
    }
    if (fd < 0 || size <= 0) return NULL;
    int prot = PROT_READ;
    if (flags & 0x2) prot |= PROT_WRITE;
    int mmapFlags = (flags & 0x1) ? MAP_PRIVATE : MAP_SHARED;

    /* 内部按页对齐的 offset / size */
    int64_t pageOff = offset & ~XFILE_PAGE_MASK;
    int64_t delta = offset - pageOff;
    int64_t mapSize = size + delta;

    void* base = mmap(NULL, (size_t)mapSize, prot, mmapFlags, fd, (off_t)pageOff);
    if (base == MAP_FAILED) return NULL;
    return (char*)base + delta;
}

bool XDeviceFile_legacyUnmap(void* addr, int64_t size) {
    if (!addr) return false;
    /* 同样需要将用户指针向下对齐到页, 并使用对齐后的 size */
    uintptr_t p = (uintptr_t)addr;
    uintptr_t base = p & ~XFILE_PAGE_MASK;
    int64_t delta = (int64_t)(p - base);
    int64_t total = size + delta;
    return munmap((void*)base, (size_t)total) == 0;
}

/* ============================================================================
 * 十、文件时间修改
 * ============================================================================ */

/**
 * @brief 通过文件描述符设置文件时间
 * @param fdx 文件描述符（XFileDescriptor 表索引）
 * @param timeType 时间类型（访问时间/修改时间/创建时间）
 * @param newDate 新的 XDateTime 时间值（使用 XinYueC 自己的日期时间类型，不再使用 C time API）
 * @return 成功返回true
 * @note 使用 futimens 直接操作已打开的 fd。
 *       路径版需求由上层通过 open→setFileTime→close 组合实现。
 */
bool XDeviceFile_legacySetFileTime(XFd fdx, XFileTime timeType, int64_t timeValue) {
    int fd = XFS_getFd(fdx);
    if (fd < 0) return false;

    struct timespec ts[2];
    ts[0].tv_nsec = UTIME_OMIT;  /* atime: 不修改 */
    ts[1].tv_nsec = UTIME_OMIT;  /* mtime: 不修改 */

    switch (timeType) {
        case XFile_AccessTime:
            ts[0].tv_sec = timeValue;
            ts[0].tv_nsec = 0;
            break;
        case XFile_ModificationTime:
        case XFile_MetadataChangeTime:
            ts[1].tv_sec = timeValue;
            ts[1].tv_nsec = 0;
            break;
        case XFile_BirthTime:
            /* POSIX 不支持独立设置创建时间，同时设置 atime/mtime 作为最佳努力 */
            ts[0].tv_sec = timeValue;
            ts[0].tv_nsec = 0;
            ts[1].tv_sec = timeValue;
            ts[1].tv_nsec = 0;
            break;
        default:
            return false;
    }

    return futimens(fd, ts) == 0;
}

/* ============================================================================
 * 十一、驱动器列表
 * ============================================================================ */

bool XDeviceFile_enumerateDrives(XDeviceFileDriveCallback callback, void* userData)
{
    if (!callback) return false;
    XString* path = XString_create_utf8("/");
    if (!path) return false;
    bool cont = callback(path, userData);
    XString_delete_base(path);
    return cont;
}

/* ============================================================================
 * 十二、存储设备信息
 * ============================================================================ */

bool XDeviceFile_getStorageInfo(const XString* path, XStorageInfoData* info) {
    if (!path || !info) return false;
    memset(info, 0, sizeof(*info));

    const char* p = XString_toUtf8(path);
    if (!p) return false;

    /* 使用 statvfs 获取文件系统信息 */
    struct statvfs vfs;
    if (statvfs(p, &vfs) != 0) return false;

    info->bytesTotal   = (int64_t)vfs.f_frsize * (int64_t)vfs.f_blocks;
    info->bytesFree    = (int64_t)vfs.f_frsize * (int64_t)vfs.f_bfree;
    info->bytesAvailable = (int64_t)vfs.f_frsize * (int64_t)vfs.f_bavail;
    info->blockSize    = (int)vfs.f_frsize;

    info->isValid = true;
    info->isReady = true;
    info->isReadOnly = (vfs.f_flag & ST_RDONLY) != 0;

    /* 获取设备路径 - 通过读取 /proc/self/mounts 匹配挂载点 */
    if (info->device) {
        FILE* fp = fopen("/proc/self/mounts", "r");
        if (fp) {
            char line[512];
            size_t bestLen = 0;
            char bestDevice[256] = "";
            while (fgets(line, sizeof(line), fp)) {
                char dev[256] = "", mnt[256] = "", fstype[64] = "", opts[64] = "";
                int n = sscanf(line, "%255s %255s %63s %63s", dev, mnt, fstype, opts);
                if (n >= 2) {
                    size_t mntLen = strlen(mnt);
                    if (mntLen <= strlen(p) && strncmp(p, mnt, mntLen) == 0) {
                        /* 检查是否匹配挂载点边界 */
                        if ((p[mntLen] == '/' || p[mntLen] == 0) && mntLen > bestLen) {
                            bestLen = mntLen;
                            strncpy(bestDevice, dev, sizeof(bestDevice) - 1);
                            bestDevice[sizeof(bestDevice) - 1] = '\0';
                            /* 设置文件系统类型 */
                            if (info->fileSystemType && n >= 3) {
                                XString_assign_utf8(info->fileSystemType, fstype);
                            }
                        }
                    }
                }
            }
            fclose(fp);
            if (bestLen > 0) {
                XString_assign_utf8(info->device, bestDevice);
            } else {
                XString_assign_utf8(info->device, "");
            }
        } else {
            XString_assign_utf8(info->device, "");
        }
    }

    /* 卷标名称 - POSIX 无标准卷标概念，使用设备名或空字符串 */
    if (info->volumeName) {
        XString_assign_utf8(info->volumeName, "");
    }

    /* 子卷名称 - Linux 可通过 Btrfs 的 ioctl 获取，暂不实现 */
    if (info->subvolume) {
        XString_assign_utf8(info->subvolume, "");
    }

    info->isRoot = (strcmp(p, "/") == 0);
    return true;
}

/* ============================================================================
 * 十三、磁盘格式化
 * ============================================================================ */

bool XDeviceFile_format(const XString* drive, XDeviceFileType fsType,
                        const XString* volumeName, int flags, int clusterSize,
                        XDeviceFileFormatProgress progress, void* userData) {
    if (!drive) return false;

    const char* devPath = XString_toUtf8(drive);
    if (!devPath || !devPath[0]) return false;

    {
        struct stat target;
        /* POSIX 格式化只接受块设备，目录和普通文件不能传给 mkfs。 */
        if (stat(devPath, &target) != 0 || !S_ISBLK(target.st_mode)) return false;
    }

    /* 映射文件系统类型到 mkfs 命令 */
    const char* mkfsCmd = NULL;
    switch (fsType) {
        case XDeviceFileType_FAT32: mkfsCmd = "mkfs.vfat";  break;
        case XDeviceFileType_NTFS:  mkfsCmd = "mkfs.ntfs";  break;
        case XDeviceFileType_exFAT: mkfsCmd = "mkfs.exfat"; break;
        case XDeviceFileType_EXT4:  mkfsCmd = "mkfs.ext4";  break;
        case XDeviceFileType_F2FS:  mkfsCmd = "mkfs.f2fs";  break;
        case XDeviceFileType_Auto:
        default:                    mkfsCmd = "mkfs.ext4";  break;
    }

    /* 构建命令行 */
    char cmd[1024];
    int cmdLen = 0;

    /* 检查 mkfs 命令是否存在 */
    {
        char whichCmd[256];
        snprintf(whichCmd, sizeof(whichCmd), "command -v %s >/dev/null 2>&1", mkfsCmd);
        if (system(whichCmd) != 0) {
            /* ??: ?? mkfs -t ?? */
            const char* fstype = NULL;
            switch (fsType) {
                case XDeviceFileType_FAT32: fstype = "vfat";  break;
                case XDeviceFileType_NTFS:  fstype = "ntfs";  break;
                case XDeviceFileType_exFAT: fstype = "exfat"; break;
                case XDeviceFileType_EXT4:  fstype = "ext4";  break;
                case XDeviceFileType_F2FS:  fstype = "f2fs";  break;
                default:                    fstype = "ext4";  break;
            }
            cmdLen = snprintf(cmd, sizeof(cmd), "mkfs -t %s", fstype);
        } else {
            cmdLen = snprintf(cmd, sizeof(cmd), "%s", mkfsCmd);
        }
    }

    /* 快速格式化标志 */
    if (flags & XFileSystemFormat_Quick) {
        if (fsType == XDeviceFileType_EXT4 || fsType == XDeviceFileType_Auto) {
            cmdLen += snprintf(cmd + cmdLen, sizeof(cmd) - cmdLen,
                               " -E lazy_itable_init=1, lazy_journal_init=1");
        }
    }

    /* 强制标志 */
    if (flags & XFileSystemFormat_Force) {
        cmdLen += snprintf(cmd + cmdLen, sizeof(cmd) - cmdLen, " -F");
    }

    /* 簇大小（仅 FAT/exFAT 生效，ext4 使用 -b） */
    if (clusterSize > 0) {
        if (fsType == XDeviceFileType_FAT32 || fsType == XDeviceFileType_exFAT) {
            cmdLen += snprintf(cmd + cmdLen, sizeof(cmd) - cmdLen,
                               " -s %d", clusterSize / 512);
        } else if (fsType == XDeviceFileType_EXT4 || fsType == XDeviceFileType_Auto) {
            cmdLen += snprintf(cmd + cmdLen, sizeof(cmd) - cmdLen,
                               " -b %d", clusterSize);
        }
    }

    /* 簇大小（仅 FAT/exFAT 生效，ext4 使用 -b） */
    if (volumeName) {
        const char* label = XString_toUtf8(volumeName);
        if (label && label[0]) {
            cmdLen += snprintf(cmd + cmdLen, sizeof(cmd) - cmdLen,
                               " -n \"%s\"", label);
        }
    }

    /* 设备路径 */
    cmdLen += snprintf(cmd + cmdLen, sizeof(cmd) - cmdLen,
                       " %s 2>&1", devPath);

    (void)cmdLen; /* 防止未使用警告 */

    if (progress) progress(10, userData);

    int result = system(cmd);

    if (progress) progress(100, userData);

    return result == 0;
}

#endif /* XFILE_USE_PLATFORM_API */

#endif /* POSIX */
