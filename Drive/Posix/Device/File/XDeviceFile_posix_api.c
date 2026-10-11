/**
 * @file XDeviceFile_posix.c
 * @brief XDeviceFile POSIX ????(Linux/macOS/BSD,?? io_uring ?? I/O)
 *
 * ???? I/O ??(read/write/copy/flush)?? io_uring ??,
 * ?? io_uring_enter ??????,?????? I/O ???
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

#if defined(__ANDROID__)
/* bionic ? shm_open(?? POSIX ????????????)??????
   ????? ASharedMemory + Binder ? fd(? Drive/Android ???,??),
   ???? ENOSYS ????,??????????,??????? */
static int xfs_posix_shm_open(const char* name, int flags, mode_t mode)
{
    (void)name; (void)flags; (void)mode;
    errno = ENOSYS;
    return -1;
}
#define shm_open xfs_posix_shm_open
#endif

/* ???????? XDeviceFile/XDevice ?? XFd ?????? */
void XDeviceFile_legacyClose(XFd fd);
int64_t XDeviceFile_legacyRead(XFd fd, void* buffer, int64_t size);
int64_t XDeviceFile_legacyWrite(XFd fd, const void* data, int64_t size);
int64_t XDeviceFile_legacySeek(XFd fd, int64_t offset, XSeekWhence whence);
bool XDeviceFile_legacyFlush(XFd fd);
bool XDeviceFile_legacyResize(XFd fd, int64_t size);
#ifdef __linux__
#if XNET_USE_IO_URING
#if XNET_BUILD_IO_URING  /* ????? XNetIoRingPosix.h */
#include <linux/io_uring.h>
#endif
#include <sys/syscall.h>
#endif
#endif

/* io_uring ????? */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif

/* ????????????(?? XFileDescriptor.object ?)?
   ???? shmFd(shm_open ??)??,????? signalFd
   (Unix domain ?????)??,XDeviceFileCommand_Map/Unmap ?? shmFd?
   ??????????????????????(XAbstractNetIoRing
   ?? io_uring ?,??????/????????):
   openSharedMemory ??????????????? RECV,?????
   waitForEvents ????? m_read.base.result;XDeviceFile_legacyRead ??
   ??????,???????????????,???????
   ??????? XObject(????? CQ ???? desc->object ??
   ?????),????? XClass_deinit_base ????? */
typedef struct XFileSharedMemoryPosix {
    XObject m_object;          /**< ??? XObject ??(???????,??????) */
    XEventContext_IO m_read;   /**< ????????????(io_uring RECV) */
    uint8_t m_readBuffer[16];  /**< ??????(?????????) */
    size_t m_rxCount;          /**< ????????????? */
    size_t m_rxOffset;         /**< ??????????? */
    bool m_readPending;        /**< ????????(SQE ??????) */
    int shmFd;                 /**< ??????? fd(shm_open ??) */
    int signalFd;              /**< ????? fd(Unix domain ?????) */
    bool created;              /**< ??????(???????????????) */
    char signalPath[128];      /**< ???????(?????) */
} XFileSharedMemoryPosix;

/* ????????????????(???"??????"??)?
   XDeviceFile_legacyRead / XDeviceFile_legacyClose ???????,??????? */
static int64_t xfs_posix_shmRead(XFd fdx, XFileSharedMemoryPosix* m,
                                 void* buf, int64_t len);
static void xfs_posix_shm_cancelRead(XFileSharedMemoryPosix* m);

/* ============================================================================
 * ????:?? XFd ???? fd
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
 * io_uring ??:???? I/O SQE ???????
 * ============================================================================ */

#ifdef __linux__

/**
 * @brief ?? io_uring ???? I/O ?????????
 * @param fd      ???????
 * @param opcode  io_uring ??? (IORING_OP_READ / IORING_OP_WRITE / IORING_OP_FSYNC)
 * @param buf     ???
 * @param len     ??
 * @param offset  ????(-1 ????????)
 * @return ?????,< 0 ????
 */
/* [死码清理] ioUringSyncIO 已删除：全仓无调用点（见审计清单）。
 */

#endif /* __linux__ */

/* ============================================================================
 * ????????
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
    {
        /* fd ??? XFd_alloc ?? XFD_INVALID ???????:
           ?????? fd(????? openSharedMemory ???????)? */
        XFd allocated = XFd_alloc(XFD_TYPE_FILE, (void*)(intptr_t)fd, NULL);
        if (allocated == XFD_INVALID) close(fd);
        return allocated;
    }
}

XFd XDeviceFile_openStandardInput(int* error)
{
    int fd;
    int flags;
    /* /dev/stdin ???????????,??? O_NONBLOCK ???
       XProcess ??????;?? POSIX ??????????? dup? */
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
    {
        /* fd ??? XFd_alloc ?? XFD_INVALID ???????:
           ?????? fd(? fcntl ????? close ??)? */
        XFd allocated = XFd_alloc(XFD_TYPE_FILE, (void*)(intptr_t)fd, NULL);
        if (allocated == XFD_INVALID) close(fd);
        return allocated;
    }
}

void XDeviceFile_legacyClose(XFd fdx) {
    XFileDescriptor* desc = XFd_get(fdx);
    if (!desc) return;
    if (desc->m_type == XFD_TYPE_MAPPING) {
        /* ?????:???????????(???? CQE),???
           ??????? fd,?????????,???????
           XObject ???????? */
        XFileSharedMemoryPosix* mapping = (XFileSharedMemoryPosix*)desc->object;
        if (mapping) {
            xfs_posix_shm_cancelRead(mapping);
            if (mapping->signalFd >= 0) close(mapping->signalFd);
            if (mapping->shmFd >= 0) close(mapping->shmFd);
            if (mapping->created && mapping->signalPath[0]) unlink(mapping->signalPath);
            /* ??????????????????????????,
               ???????????????? */
            XCoreApplication_removePostedEvents((XObject*)&mapping->m_object, 0);
            XClassDeinit((XClass*)&mapping->m_object);
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

    /* ??????????:?????????????(io_uring ?)? */
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

    /* ??????????;???????????????? */
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

    /* ??????????:??????????(??????,
       ???????????,????????)? */
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
 * ????????
 * ============================================================================ */

/* Unix ??? -> Qt XFilePermissions ?? */
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

/* Qt XFilePermissions -> Unix ??? ?? */
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
 * ????????
 * ============================================================================ */

/* ============================================================================
 * ??? (XDG Trash ??)
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

    /* 1. ?? $XDG_DATA_HOME ? $HOME/.local/share */
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

    /* 2. ????? basename, ???? XDateTime ????? */
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

    /* 3. rename(2) ?????????; ??????? copy+unlink */
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
    /* Qt ??: ???????? rename ?? */
    if (XDeviceFile_exists(newPath)) return false;
    return rename(XString_toUtf8(oldPath), XString_toUtf8(newPath)) == 0;
}

bool XDeviceFile_renameReplace(const XString* oldPath, const XString* newPath) {
    if (!oldPath || !newPath) return false;
    /* POSIX rename(2) ????????;? XDeviceFile_rename ? Qt
     * ???????(XSaveFile ??????)????? rename(2),
     * ??? rename(??? exists ??,???????)? */
    return rename(XString_toUtf8(oldPath), XString_toUtf8(newPath)) == 0;
}

bool XDeviceFile_copy(const XString* srcPath, const XString* dstPath) {
    if (!srcPath || !dstPath) return false;
    /* Qt ??: ???????? copy ?? */
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
 * ??????
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

    /* ????:??????,?????,????? */
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
                XClassDelete(subPath);
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
 * ??????
 * ============================================================================ */

/* ??????? .?.. ??????,?????????????????? */
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
 * ??????
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
 * ????????
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
 * ??????
 * ============================================================================ */

bool XDeviceFile_setPermissions(const XString* path, XFilePermissions permissions) {
    if (!path) return false;
    return chmod(XString_toUtf8(path), xPermsToUnixMode(permissions)) == 0;
}

/* ============================================================================
 * ??????(3?)- ??
 * ============================================================================
 *
 * ?????? POSIX ??????:
 *   1. ???????(shm_open):???????,?? XDeviceFileCommand_Map ??;
 *   2. ??????(Unix domain ?????,?? <?????>.sig):
 *      ??????????????? 1 ?????,???????
 *      ??????(XAbstractNetIoRing ?? io_uring ?)????,
 *      ??????/??????????,?????????????
 *      ??????????,??????? API?
 *
 * XFd ???????? fd,object ??????? fd?????????
 * ?????;XDeviceFile_legacyRead / XDeviceFile_legacyWrite ????????
 * ????,XDeviceFileCommand_Map / XDeviceFile_legacyClose ??????????
 */

/* ????????(? P_tmpdir ??? /tmp)? */
#ifndef XFILE_SHM_SIGNAL_DIR
#define XFILE_SHM_SIGNAL_DIR "/tmp"
#endif

/* ?????????????(??):XDeviceFile_legacyRead ???????
   ?????? waitForEvents ?????????? 0,???????
   ????;????????????,???????? */
#define XFILE_SHM_SIGNAL_RCVTIMEO_MS 500

/* ?????? connect ????????:
   ????????? bind/listen ? accept,????????? bind ?
   ?? connect(ENOENT/ECONNREFUSED),????????????? */
#define XFILE_SHM_SIGNAL_CONNECT_RETRY 100
#define XFILE_SHM_SIGNAL_CONNECT_WAIT_MS 20

/* ???"????"????:m_read.base.result ?????? SQE ???
   ?????????????;??? result ???????
   (>=0 ???,<0 ?? errno)? */
#define XFS_SHM_READ_PENDING INT64_MIN

/* ?????????????????????????
   (?? XFILE_SHM_SIGNAL_RCVTIMEO_MS,????????)? */
#define XFS_SHM_CANCEL_RECLAIM_TRIES 50

/* ?????????????????????:
   ???????? ._-,??????? '_',?? shm ???? '/' ?
   ?????????????(?????)?? false? */
static bool xfs_posix_makeSignalPath(const char* name, char* out, size_t outSize)
{
    size_t i, n;
    if (!name || !out || outSize == 0) return false;
    n = strlen(name);
    if (n > outSize - 64) return false; /* ????????? */
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

/* ???????(???):bind + listen ??? accept,??????,
   ????????? accept ????(create=true ????????
   openSharedMemory(create=false) ????,????)??????? fd,
   ???? -1?????????????:??????? io_uring ???
   ?????? */
static int xfs_posix_signalListen(const char* path)
{
    int fd;
    struct sockaddr_un addr;
    int opt = 1;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    /* ??????????????(??????)? */
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

/* ???????(???):???????????????,
   ????????? ENOENT/ECONNREFUSED ?????? */
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
 * ????????(???????)
 *
 * ??????????/??????:openSharedMemory ???????
 * ???????? RECV(IORING_OP_RECV,??? XAbstractNetIoRing
 * ?? io_uring ?),????????? waitForEvents(?? poll
 * ring fd)????? m_read.base.result;XDeviceFile_legacyRead ??
 * m_readBuffer ????,?????????????????(???
 * ?? 0 ????????????),????????????????
 * ============================================================================ */

/* ?????????(io_uring IORING_OP_RECV)?
   ?? mapping->m_readBuffer,?????????????
   mapping->m_read.base.result????? true;?????????
   ?? false????????????? */
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
    m->m_read.base.eventMask = XSocketActType_Read;
    m->m_read.base.buffer = m->m_readBuffer;
    m->m_read.base.bufferSize = sizeof(m->m_readBuffer);
    m->m_read.base.result = XFS_SHM_READ_PENDING; /* ??:???? */
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

/* ???????????????????(????):
   ???????????????????;?????,??
   XFILE_SHM_SIGNAL_RCVTIMEO_MS ?????????????
   waitForEvents(?? poll ring fd,????????)?
   ?? waitForEvents ????????(wakeUp ????????
   ? CQ ??)????,???????????????,????
   ???????;????????????? false(?????
   ????????)???? m_read.base.result ???????
   ?? true ???????,false ?????????? */
static bool xfs_posix_shm_waitReadable(XFileSharedMemoryPosix* m)
{
    XAbstractNetIoRing* ring;
    int64_t deadline;
    int64_t now;

    if (!m || !m->m_readPending) return false;
    if (m->m_read.base.result != XFS_SHM_READ_PENDING)
        return true; /* ??????????? */
    ring = XAbstractNetIoRing_global();
    if (!ring) return false;

    /* ???????????,???????????;
       waitForEvents ???????????,??????????? */
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

/* ??????????????(XDeviceFile_legacyClose ?????):
   ?? IORING_OP_ASYNC_CANCEL ???????????????? CQE
   ??(result ??? -ECANCELED ?????),???? mapping ?
   ????? CQE ??????????? */
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
    sqe->user_data = 0; /* ?????????????,? CQE ???? */
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

/* XDeviceFile_legacyRead ???????????:
   ??????????;??????????????????,
   ????? 0(???????????),????/?????? -1? */
static int64_t xfs_posix_shmRead(XFd fdx, XFileSharedMemoryPosix* m,
                                 void* buf, int64_t len)
{
    size_t n;

    if (!m || m->signalFd < 0) return -1;
    if (len <= 0) return 0;

    /* 1. ??????????????;??????????????
          ???,???????????????(????????)? */
    if (m->m_rxCount > 0) {
        n = ((size_t)len < m->m_rxCount) ? (size_t)len : m->m_rxCount;
        memcpy(buf, m->m_readBuffer + m->m_rxOffset, n);
        m->m_rxOffset += n;
        m->m_rxCount -= n;
        if (m->m_rxCount == 0)
            (void)xfs_posix_shm_armRead(fdx, m);
        return (int64_t)n;
    }

    /* 2. ????(????????)?????:????????,
          ??????????;????(XCoreApplication ????)
          ???????? */
    if (!XAbstractNetIoRing_global()) {
        ssize_t r = read(m->signalFd, buf, (size_t)len);
        return (r >= 0) ? (int64_t)r : -1;
    }

    /* 3. ????????,???????????(????)? */
    if (!m->m_readPending) {
        if (!xfs_posix_shm_armRead(fdx, m)) return -1;
    }
    if (!xfs_posix_shm_waitReadable(m))
        return 0; /* ???:???????????? */

    m->m_readPending = false;
    {
        int64_t res = m->m_read.base.result;
        m->m_read.base.result = XFS_SHM_READ_PENDING; /* ???? */
        if (res <= 0) {
            /* 0=????(EOF),?=????:????????? */
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

    /* ????????????????(???????,???
       ??? open ? startAsyncRead ????)? */
    (void)xfs_posix_shm_armRead(result, mapping);

    /* n ? XString_toUtf8 ???????(XString ????),
       ? name ?????,???????? */
    return result;

fail:
    if (mapping) {
        if (mapping->signalFd >= 0) close(mapping->signalFd);
        if (mapping->created && mapping->signalPath[0]) unlink(mapping->signalPath);
        XClassDeinit((XClass*)&mapping->m_object);
        XFree_System(mapping);
    } else {
        if (signalFd >= 0) { close(signalFd); if (create && sigPath[0]) unlink(sigPath); }
    }
    if (shmFd >= 0) close(shmFd);
    /* n ?????(XString ?? UTF-8 ??),??????,
       ?? name ??????????????? */
    return XFD_INVALID;
}

/* Linux mmap ?? offset ? size ???????.
   ?????? offset/size, ??????? offset ??? size, ????????????. */
#include <unistd.h>
#define XFILE_PAGE_SIZE 4096
#define XFILE_PAGE_MASK (XFILE_PAGE_SIZE - 1)

void* XDeviceFile_legacyMap(XFd fdx, int64_t offset, int64_t size, int flags) {
    XFileDescriptor* desc = XFd_get(fdx);
    int fd;
    if (!desc) return NULL;
    if (desc->m_type == XFD_TYPE_MAPPING) {
        /* ?????:????????,???? fd ??? object? */
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

    /* ??????? offset / size */
    int64_t pageOff = offset & ~XFILE_PAGE_MASK;
    int64_t delta = offset - pageOff;
    int64_t mapSize = size + delta;

    void* base = mmap(NULL, (size_t)mapSize, prot, mmapFlags, fd, (off_t)pageOff);
    if (base == MAP_FAILED) return NULL;
    return (char*)base + delta;
}

bool XDeviceFile_legacyUnmap(void* addr, int64_t size) {
    if (!addr) return false;
    /* ???????????????, ??????? size */
    uintptr_t p = (uintptr_t)addr;
    uintptr_t base = p & ~XFILE_PAGE_MASK;
    int64_t delta = (int64_t)(p - base);
    int64_t total = size + delta;
    return munmap((void*)base, (size_t)total) == 0;
}

/* ============================================================================
 * ????????
 * ============================================================================ */

/**
 * @brief ?????????????
 * @param fdx ?????(XFileDescriptor ???)
 * @param timeType ????(????/????/????)
 * @param newDate ?? XDateTime ???(?? XinYueC ?????????,???? C time API)
 * @return ????true
 * @note ?? futimens ???????? fd?
 *       ?????????? open?setFileTime?close ?????
 */
bool XDeviceFile_legacySetFileTime(XFd fdx, XFileTime timeType, int64_t timeValue) {
    int fd = XFS_getFd(fdx);
    if (fd < 0) return false;

    struct timespec ts[2];
    ts[0].tv_nsec = UTIME_OMIT;  /* atime: ??? */
    ts[1].tv_nsec = UTIME_OMIT;  /* mtime: ??? */

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
            /* POSIX ???????????,???? atime/mtime ?????? */
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
 * ????????
 * ============================================================================ */

bool XDeviceFile_enumerateDrives(XDeviceFileDriveCallback callback, void* userData)
{
    if (!callback) return false;
    XString* path = XString_create_utf8("/");
    if (!path) return false;
    bool cont = callback(path, userData);
    XClassDelete(path);
    return cont;
}

/* ============================================================================
 * ?????????
 * ============================================================================ */

bool XDeviceFile_getStorageInfo(const XString* path, XStorageInfoData* info) {
    if (!path || !info) return false;
    memset(info, 0, sizeof(*info));

    const char* p = XString_toUtf8(path);
    if (!p) return false;

    /* ?? statvfs ???????? */
    struct statvfs vfs;
    if (statvfs(p, &vfs) != 0) return false;

    info->bytesTotal   = (int64_t)vfs.f_frsize * (int64_t)vfs.f_blocks;
    info->bytesFree    = (int64_t)vfs.f_frsize * (int64_t)vfs.f_bfree;
    info->bytesAvailable = (int64_t)vfs.f_frsize * (int64_t)vfs.f_bavail;
    info->blockSize    = (int)vfs.f_frsize;

    info->isValid = true;
    info->isReady = true;
    info->isReadOnly = (vfs.f_flag & ST_RDONLY) != 0;

    /* ?????? - ???? /proc/self/mounts ????? */
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
                        /* ??????????? */
                        if ((p[mntLen] == '/' || p[mntLen] == 0) && mntLen > bestLen) {
                            bestLen = mntLen;
                            strncpy(bestDevice, dev, sizeof(bestDevice) - 1);
                            bestDevice[sizeof(bestDevice) - 1] = '\0';
                            /* ???????? */
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

    /* ???? - POSIX ???????,?????????? */
    if (info->volumeName) {
        XString_assign_utf8(info->volumeName, "");
    }

    /* ???? - Linux ??? Btrfs ? ioctl ??,???? */
    if (info->subvolume) {
        XString_assign_utf8(info->subvolume, "");
    }

    info->isRoot = (strcmp(p, "/") == 0);
    return true;
}

/* ============================================================================
 * ????????
 * ============================================================================ */

bool XDeviceFile_format(const XString* drive, XDeviceFileType fsType,
                        const XString* volumeName, int flags, int clusterSize,
                        XDeviceFileFormatProgress progress, void* userData) {
    if (!drive) return false;

    const char* devPath = XString_toUtf8(drive);
    if (!devPath || !devPath[0]) return false;

    {
        struct stat target;
        /* POSIX ?????????,??????????? mkfs? */
        if (stat(devPath, &target) != 0 || !S_ISBLK(target.st_mode)) return false;
    }

    /* ????????? mkfs ?? */
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

    /* ????? */
    char cmd[1024];
    int cmdLen = 0;

    /* ?? mkfs ?????? */
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

    /* ??????? */
    if (flags & XFileSystemFormat_Quick) {
        if (fsType == XDeviceFileType_EXT4 || fsType == XDeviceFileType_Auto) {
            cmdLen += snprintf(cmd + cmdLen, sizeof(cmd) - cmdLen,
                               " -E lazy_itable_init=1, lazy_journal_init=1");
        }
    }

    /* ???? */
    if (flags & XFileSystemFormat_Force) {
        cmdLen += snprintf(cmd + cmdLen, sizeof(cmd) - cmdLen, " -F");
    }

    /* ???(? FAT/exFAT ??,ext4 ?? -b) */
    if (clusterSize > 0) {
        if (fsType == XDeviceFileType_FAT32 || fsType == XDeviceFileType_exFAT) {
            cmdLen += snprintf(cmd + cmdLen, sizeof(cmd) - cmdLen,
                               " -s %d", clusterSize / 512);
        } else if (fsType == XDeviceFileType_EXT4 || fsType == XDeviceFileType_Auto) {
            cmdLen += snprintf(cmd + cmdLen, sizeof(cmd) - cmdLen,
                               " -b %d", clusterSize);
        }
    }

    /* ???(? FAT/exFAT ??,ext4 ?? -b) */
    if (volumeName) {
        const char* label = XString_toUtf8(volumeName);
        if (label && label[0]) {
            cmdLen += snprintf(cmd + cmdLen, sizeof(cmd) - cmdLen,
                               " -n \"%s\"", label);
        }
    }

    /* ???? */
    cmdLen += snprintf(cmd + cmdLen, sizeof(cmd) - cmdLen,
                       " %s 2>&1", devPath);

    (void)cmdLen; /* ??????? */

    if (progress) progress(10, userData);

    int result = system(cmd);

    if (progress) progress(100, userData);

    return result == 0;
}

#endif /* XFILE_USE_PLATFORM_API */

#endif /* POSIX */
