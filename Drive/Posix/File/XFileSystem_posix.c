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
#include <pwd.h>
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
    memset(out, 0, sizeof(*out));
    out->exists = true;
    out->size = st->st_size;
    out->metadataChangeTime = st->st_ctime;
    out->modificationTime = st->st_mtime;
    out->accessTime = st->st_atime;
    out->isDir = S_ISDIR(st->st_mode);
    out->isFile = S_ISREG(st->st_mode);
    out->isSymLink = S_ISLNK(st->st_mode);
    out->permissions = st->st_mode & 0777;
#ifdef __APPLE__
    out->birthTime = st->st_birthtime;
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    out->birthTime = st->st_birthtime;
#else
    /* Linux: stx.btime or st_ctime as fallback */
    out->birthTime = st->st_ctime;
#endif
}

bool XFileSystem_stat(const XString* path, XFileStat* out) {
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

bool XFileSystem_fstat(XFd fdx, XFileStat* out) {
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

bool XFileSystem_rmdir(const XString* path, bool recursive) {
    if (!path) return false;
    const char* p = XString_toUtf8(path);

    if (!recursive) {
        return rmdir(p) == 0;
    }

    /* 递归删除：遍历目录内容，先删除子项，再删除目录 */
    DIR* d = opendir(p);
    if (!d) return false;

    struct dirent* de;
    char fullPath[PATH_MAX];
    size_t baseLen = strlen(p);

    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        int n = snprintf(fullPath, sizeof(fullPath), "%s/%s", p, de->d_name);
        if (n < 0 || (size_t)n >= sizeof(fullPath)) continue;

        struct stat st;
        if (lstat(fullPath, &st) == 0 && S_ISDIR(st.st_mode)) {
            XString* subPath = XString_create_utf8(fullPath);
            if (subPath) {
                XFileSystem_rmdir(subPath, true);
                XString_delete_base(subPath);
            }
        } else {
            unlink(fullPath);
        }
    }
    closedir(d);
    return rmdir(p) == 0;
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

bool XFileSystem_getSpecialPath(XSpecialPath type, XString* path) {
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

bool XFileSystem_setCurrentPath(const XString* path) {
    if (!path) return false;
    return chdir(XString_toUtf8(path)) == 0;
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
 * 十、文件时间修改
 * ============================================================================ */

/**
 * @brief 通过文件描述符设置文件时间
 * @param fdx 文件描述符（XFileDescriptor 表索引）
 * @param timeType 时间类型（访问时间/修改时间/创建时间）
 * @param timeValue 时间值（Unix时间戳，秒）
 * @return 成功返回true
 * @note 使用 futimens 直接操作已打开的 fd。
 *       路径版需求由上层通过 open→setFileTime→close 组合实现。
 */
bool XFileSystem_setFileTime(XFd fdx, XFileTime timeType, int64_t timeValue) {
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

int XFileSystem_drives_count(void) {
    return 1;
}

bool XFileSystem_drives_at(int index, XString* path) {
    if (index != 0 || !path) return false;
    XString_assign_utf8(path, "/");
    return true;
}

/* ============================================================================
 * 十二、存储设备信息
 * ============================================================================ */

bool XFileSystem_getStorageInfo(const XString* path, XStorageInfoData* info) {
    if (!path || !info) return false;
    memset(info, 0, sizeof(*info));
    return true;
}

/* ============================================================================
 * 十三、磁盘格式化
 * ============================================================================ */

bool XFileSystem_format(const XString* drive, XFileSystemType fsType,
                        const XString* volumeName, int flags, int clusterSize,
                        XFileSystemFormatProgress progress, void* userData) {
    if (!drive) return false;

    const char* devPath = XString_toUtf8(drive);
    if (!devPath || !devPath[0]) return false;

    /* 映射文件系统类型到 mkfs 命令 */
    const char* mkfsCmd = NULL;
    switch (fsType) {
        case XFileSystemType_FAT32: mkfsCmd = "mkfs.vfat";  break;
        case XFileSystemType_NTFS:  mkfsCmd = "mkfs.ntfs";  break;
        case XFileSystemType_exFAT: mkfsCmd = "mkfs.exfat"; break;
        case XFileSystemType_EXT4:  mkfsCmd = "mkfs.ext4";  break;
        case XFileSystemType_F2FS:  mkfsCmd = "mkfs.f2fs";  break;
        case XFileSystemType_Auto:
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
                case XFileSystemType_FAT32: fstype = "vfat";  break;
                case XFileSystemType_NTFS:  fstype = "ntfs";  break;
                case XFileSystemType_exFAT: fstype = "exfat"; break;
                case XFileSystemType_EXT4:  fstype = "ext4";  break;
                case XFileSystemType_F2FS:  fstype = "f2fs";  break;
                default:                    fstype = "ext4";  break;
            }
            cmdLen = snprintf(cmd, sizeof(cmd), "mkfs -t %s", fstype);
        } else {
            cmdLen = snprintf(cmd, sizeof(cmd), "%s", mkfsCmd);
        }
    }

    /* 快速格式化标志 */
    if (flags & XFileSystemFormat_Quick) {
        if (fsType == XFileSystemType_EXT4 || fsType == XFileSystemType_Auto) {
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
        if (fsType == XFileSystemType_FAT32 || fsType == XFileSystemType_exFAT) {
            cmdLen += snprintf(cmd + cmdLen, sizeof(cmd) - cmdLen,
                               " -s %d", clusterSize / 512);
        } else if (fsType == XFileSystemType_EXT4 || fsType == XFileSystemType_Auto) {
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

#endif /* POSIX */
