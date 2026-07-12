/**
 * @file XFileSystem_posix.c
 * @brief XFileSystem POSIX 平台实现（Linux/macOS/BSD，支持 io_uring 异步 I/O）
 *
 * 核心文件 I/O 操作（read/write/copy/flush）通过 io_uring 提交，
 * 使用 io_uring_enter 同步等待完成，实现真正异步 I/O 路径。
 */

#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)

#include "XFileSystem_platform.h"
#include "XFileInfo.h"
#include "XFileSystem_config.h"
#include "XStorageInfo.h"
#include "XString.h"
#include "XMemory.h"
#include "XFileDescriptor.h"
#include "XDateTime.h"
#include "XAbstractNetIoRing.h"
#include "XNetIoRingPosix.h"
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
#include <utime.h>
#include <limits.h>
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

/* ============================================================================
 * 内部辅助：通过 XFd 获取底层 fd
 * ============================================================================ */

static int XFS_getFd(XFd fdx) {
    XFileDescriptor* desc = XFd_get(fdx);
    if (!desc) return -1;
    return (int)(intptr_t)desc->handle;
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

XFd XFileSystem_open(const XString* path, int mode, int* error) {
    if (!path) { if (error) *error = EINVAL; return XFD_INVALID; }
    const char* p = XString_toUtf8(path);
    int flags = 0;
    if (mode & XFileSystem_ReadOnly) flags |= O_RDONLY;
    if (mode & XFileSystem_WriteOnly) flags |= (mode & XFileSystem_ReadOnly) ? O_RDWR : O_WRONLY;
    if (mode & XFileSystem_Append) flags |= O_APPEND;
    if (mode & XFileSystem_Truncate) flags |= O_TRUNC;
    if (mode & XFileSystem_NewOnly) flags |= O_CREAT | O_EXCL;
    if (!(mode & XFileSystem_NewOnly) && (mode & XFileSystem_WriteOnly)) flags |= O_CREAT;

    int fd = open(p, flags, 0666);
    if (fd < 0) { if (error) *error = errno; return XFD_INVALID; }
    if (error) *error = 0;
    return XFd_alloc(XFD_TYPE_FILE, (void*)(intptr_t)fd, NULL);
}

void XFileSystem_close(XFd fdx) {
    XFileDescriptor* desc = XFd_get(fdx);
    if (!desc) return;
    int fd = (int)(intptr_t)desc->handle;
    close(fd);
    XFd_free(fdx);
}

int64_t XFileSystem_pos(XFd fdx) {
    int fd = XFS_getFd(fdx);
    if (fd < 0) return -1;
    return (int64_t)lseek(fd, 0, SEEK_CUR);
}

bool XFileSystem_seek(XFd fdx, int64_t pos) {
    int fd = XFS_getFd(fdx);
    if (fd < 0) return false;
    return lseek(fd, pos, SEEK_SET) >= 0;
}

int64_t XFileSystem_read(XFd fdx, void* buf, int64_t len) {
    int fd = XFS_getFd(fdx);
    if (fd < 0) return -1;

#ifdef __linux__
    /* 使用 io_uring 异步读取（同步等待完成） */
    if (XAbstractNetIoRing_global()) {
        return ioUringSyncIO(fd, IORING_OP_READ, buf, (uint64_t)len, -1);
    }
#endif
    ssize_t n = read(fd, buf, (size_t)len);
    return (n >= 0) ? (int64_t)n : -1;
}

int64_t XFileSystem_write(XFd fdx, const void* buf, int64_t len) {
    int fd = XFS_getFd(fdx);
    if (fd < 0) return -1;

#ifdef __linux__
    /* 使用 io_uring 异步写入（同步等待完成） */
    if (XAbstractNetIoRing_global()) {
        return ioUringSyncIO(fd, IORING_OP_WRITE, (void*)buf, (uint64_t)len, -1);
    }
#endif
    ssize_t n = write(fd, buf, (size_t)len);
    return (n >= 0) ? (int64_t)n : -1;
}

bool XFileSystem_flush(XFd fdx) {
    int fd = XFS_getFd(fdx);
    if (fd < 0) return false;

#ifdef __linux__
    /* 使用 io_uring fsync（同步等待完成） */
    if (XAbstractNetIoRing_global()) {
        return ioUringSyncIO(fd, IORING_OP_FSYNC, NULL, 0, -1) >= 0;
    }
#endif
    return fsync(fd) == 0;
}

bool XFileSystem_resize(XFd fdx, int64_t size) {
    int fd = XFS_getFd(fdx);
    if (fd < 0) return false;
    return ftruncate(fd, size) == 0;
}

/* ============================================================================
 * 二、文件属性操作
 * ============================================================================ */

static void fillStat(struct stat* st, XFileStat* out) {
    out->size = st->st_size;
    out->birthTime = 0;
    out->metadataChangeTime = st->st_ctime;
    out->modificationTime = st->st_mtime;
    out->accessTime = st->st_atime;
    out->isDir = S_ISDIR(st->st_mode);
    out->isFile = S_ISREG(st->st_mode);
    out->isSymLink = S_ISLNK(st->st_mode);
    out->permissions = st->st_mode & 0777;
}

bool XFileSystem_stat(const XString* path, XFileStat* out) {
    if (!path || !out) return false;
    struct stat st;
    if (((int (*)(const char*, struct stat*))stat)(XString_toUtf8(path), &st) != 0) return false;
    fillStat(&st, out);
    return true;
}

bool XFileSystem_fstat(XFd fdx, XFileStat* out) {
    int fd = XFS_getFd(fdx);
    if (fd < 0 || !out) return false;
    struct stat st;
    if (((int (*)(int, struct stat*))fstat)(fd, &st) != 0) return false;
    fillStat(&st, out);
    return true;
}

/* ============================================================================
 * 三、文件系统操作
 * ============================================================================ */

bool XFileSystem_exists(const XString* path) {
    if (!path) return false;
    return access(XString_toUtf8(path), F_OK) == 0;
}

bool XFileSystem_remove(const XString* path) {
    if (!path) return false;
    return unlink(XString_toUtf8(path)) == 0;
}

bool XFileSystem_rename(const XString* oldPath, const XString* newPath) {
    if (!oldPath || !newPath) return false;
    return rename(XString_toUtf8(oldPath), XString_toUtf8(newPath)) == 0;
}

bool XFileSystem_copy(const XString* srcPath, const XString* dstPath) {
    if (!srcPath || !dstPath) return false;
    const char* src = XString_toUtf8(srcPath);
    const char* dst = XString_toUtf8(dstPath);
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) return false;
    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dfd < 0) { close(sfd); return false; }

    char buf[8192];
    bool ok = true;

#ifdef __linux__
    if (XAbstractNetIoRing_global()) {
        /* 使用 io_uring 异步复制 */
        int64_t offset = 0;
        while (1) {
            int64_t nr = ioUringSyncIO(sfd, IORING_OP_READ, buf, sizeof(buf), offset);
            if (nr <= 0) { ok = (nr == 0); break; }
            int64_t nw = ioUringSyncIO(dfd, IORING_OP_WRITE, buf, (uint64_t)nr, offset);
            if (nw != nr) { ok = false; break; }
            offset += nr;
        }
        ioUringSyncIO(dfd, IORING_OP_FSYNC, NULL, 0, -1);
    } else
#endif
    {
        ssize_t n;
        while ((n = read(sfd, buf, sizeof(buf))) > 0) {
            if (write(dfd, buf, (size_t)n) != n) { ok = false; break; }
        }
    }
    close(sfd);
    close(dfd);
    return ok;
}

/* ============================================================================
 * 四、目录操作
 * ============================================================================ */

bool XFileSystem_mkdir(const XString* path, bool recursive) {
    if (!path) return false;
    const char* p = XString_toUtf8(path);
    if (recursive) {
        char tmp[1024];
        strncpy(tmp, p, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        for (char* s = tmp + 1; *s; s++) {
            if (*s == '/') {
                *s = '\0';
                mkdir(tmp, 0755);
                *s = '/';
            }
        }
    }
    return mkdir(p, 0755) == 0 || errno == EEXIST;
}

bool XFileSystem_rmdir(const XString* path) {
    if (!path) return false;
    return rmdir(XString_toUtf8(path)) == 0;
}

typedef struct { DIR* dir; } PosixDirIter;

XDirIterator XFileSystem_opendir(const XString* path) {
    if (!path) return NULL;
    DIR* d = opendir(XString_toUtf8(path));
    if (!d) return NULL;
    PosixDirIter* iter = (PosixDirIter*)XMalloc_System(sizeof(PosixDirIter));
    if (!iter) { closedir(d); return NULL; }
    iter->dir = d;
    return (XDirIterator)iter;
}

bool XFileSystem_readdir(XDirIterator iter, XDirEntry* entry) {
    if (!iter || !entry) return false;
    PosixDirIter* it = (PosixDirIter*)iter;
    struct dirent* de = readdir(it->dir);
    if (!de) return false;
    XString_assign_utf8(entry->name, de->d_name);
    entry->isDir = (de->d_type == DT_DIR);
    entry->isFile = (de->d_type == DT_REG);
    entry->isSymLink = (de->d_type == DT_LNK);
    entry->isHidden = (de->d_name[0] == '.');
    return true;
}

void XFileSystem_closedir(XDirIterator iter) {
    if (!iter) return;
    PosixDirIter* it = (PosixDirIter*)iter;
    closedir(it->dir);
    XFree_System(it);
}

/* ============================================================================
 * 五、路径操作
 * ============================================================================ */

bool XFileSystem_resolvePath(const XString* path, XString* result, XPathStyle style) {
    if (!path || !result) return false;
    char buf[1024];
    if (style == XPathStyle_Canonical) {
        if (!realpath(XString_toUtf8(path), buf)) return false;
    } else {
        if (!realpath(XString_toUtf8(path), buf)) return false;
    }
    XString_assign_utf8(result, buf);
    return true;
}

/* ============================================================================
 * 六、特殊路径
 * ============================================================================ */

bool XFileSystem_currentPath(XString* path) {
    if (!path) return false;
    char buf[1024];
    if (!getcwd(buf, sizeof(buf))) return false;
    XString_assign_utf8(path, buf);
    return true;
}

bool XFileSystem_setCurrentPath(const XString* path) {
    if (!path) return false;
    return chdir(XString_toUtf8(path)) == 0;
}

bool XFileSystem_homePath(XString* path) {
    if (!path) return false;
    const char* home = getenv("HOME");
    if (!home) home = "/";
    XString_assign_utf8(path, home);
    return true;
}

bool XFileSystem_rootPath(XString* path) {
    if (!path) return false;
    XString_assign_utf8(path, "/");
    return true;
}

bool XFileSystem_tempPath(XString* path) {
    if (!path) return false;
    const char* tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
    XString_assign_utf8(path, tmp);
    return true;
}

/* ============================================================================
 * 七、符号链接操作
 * ============================================================================ */

bool XFileSystem_link(const XString* targetPath, const XString* linkPath) {
    if (!targetPath || !linkPath) return false;
    return symlink(XString_toUtf8(targetPath), XString_toUtf8(linkPath)) == 0;
}

bool XFileSystem_readLink(const XString* path, XString* target) {
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

bool XFileSystem_setPermissions(const XString* path, XFilePermissions permissions) {
    if (!path) return false;
    return chmod(XString_toUtf8(path), permissions) == 0;
}

/* ============================================================================
 * 九、内存映射
 * ============================================================================ */

void* XFileSystem_map(XFd fdx, int64_t offset, int64_t size, bool writable) {
    int fd = XFS_getFd(fdx);
    if (fd < 0) return NULL;
    int prot = PROT_READ;
    if (writable) prot |= PROT_WRITE;
    void* addr = mmap(NULL, size, prot, MAP_SHARED, fd, offset);
    return (addr == MAP_FAILED) ? NULL : addr;
}

bool XFileSystem_unmap(void* addr, int64_t size) {
    if (!addr) return false;
    return munmap(addr, size) == 0;
}

/* ============================================================================
 * 十、递归删除目录
 * ============================================================================ */

bool XFileSystem_rmdir_recursive(const XString* path) {
    if (!path) return false;
    const char* p = XString_toUtf8(path);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", p);
    return system(cmd) == 0;
}

/* ============================================================================
 * 十一、文件时间修改
 * ============================================================================ */

bool XFileSystem_setFileTime(const XString* path, XFileTime timeType, int64_t timeValue) {
    if (!path) return false;
    struct utimbuf ut;
    struct stat st;
    if (stat(XString_toUtf8(path), &st) != 0) return false;
    ut.actime = st.st_atime;
    ut.modtime = st.st_mtime;
    switch (timeType) {
    case XFile_AccessTime: ut.actime = timeValue; break;
    case XFile_ModificationTime: ut.modtime = timeValue; break;
    default: break;
    }
    return utime(XString_toUtf8(path), &ut) == 0;
}

bool XFileSystem_fsetFileTime(XFd fdx, XFileTime timeType, int64_t timeValue) {
    int fd = XFS_getFd(fdx);
    if (fd < 0) return false;
    struct timespec ts[2] = {{0, UTIME_OMIT}, {0, UTIME_OMIT}};
    switch (timeType) {
    case XFile_AccessTime: ts[0].tv_sec = timeValue; ts[0].tv_nsec = 0; break;
    case XFile_ModificationTime: ts[1].tv_sec = timeValue; ts[1].tv_nsec = 0; break;
    default: break;
    }
    return futimens(fd, ts) == 0;
}

/* ============================================================================
 * 十二、驱动器列表
 * ============================================================================ */

int XFileSystem_drives_count(void) {
    return 1;
}

bool XFileSystem_drives_at(int index, XString* path) {
    if (index != 0 || !path) return false;
    XString_assign_utf8(path, "/");
    return true;
}

/* ============================================================================
 * 十三、存储设备信息
 * ============================================================================ */

bool XFileSystem_getStorageInfo(const XString* path, XStorageInfoData* info) {
    if (!path || !info) return false;
    memset(info, 0, sizeof(*info));
    return true;
}

/* ============================================================================
 * 十四、磁盘格式化
 * ============================================================================ */

bool XFileSystem_format(const XString* drive, XFileSystemType fsType,
                        const XString* volumeName, int flags, int clusterSize,
                        XFileSystemFormatProgress progress, void* userData) {
    (void)drive; (void)fsType; (void)volumeName; (void)flags;
    (void)clusterSize; (void)progress; (void)userData;
    return false;
}

#endif /* POSIX */