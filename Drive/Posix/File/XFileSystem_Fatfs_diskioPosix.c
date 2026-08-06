/**
 * @file XFileSystem_Fatfs_diskioPosix.c
 * @brief FatFs POSIX 平台磁盘 I/O 实现（Linux/macOS/BSD）
 *
 * 对标 Windows XFileSystem_Fatfs_diskioWin32.c。
 *
 * 两种磁盘 I/O 模式（通过 XFILE_FATFS_DISKIO_MODE 配置）：
 *   0 = 文件镜像模式：使用普通文件作为 FatFs 磁盘镜像（默认）
 *   1 = 物理磁盘模式：直接访问块设备（如 /dev/sda1）
 *
 * 平台抽象层实现（XFileSystem_Fatfs_platform.h 声明的函数）：
 *   - XFatfsDrives_count / XFatfsDrives_at / XFatfsDrives_prefixToIndex
 *   - XFatfsPath_home / XFatfsPath_root / XFatfsPath_current
 *   - XFatfsPath_setCurrent / XFatfsPath_temp
 */

#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)

#include "XFileSystem_config.h"

/* 仅在启用 FatFS 模式时编译 */
#if defined(XFILE_USE_FATFS)

#include "ff.h"           /* LBA_t, DSTATUS, DRESULT, BYTE 等 */
#include "diskio.h"
#include "XFileSystem_Fatfs_platform.h"
#include "XString.h"
#include "XMemory.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <limits.h>

/* ============================================================================
 * 磁盘 I/O 模式配置
 * ============================================================================ */

#ifndef XFILE_FATFS_DISKIO_MODE
#define XFILE_FATFS_DISKIO_MODE      0    /* 0=文件镜像, 1=物理磁盘 */
#endif

#ifndef XFILE_FATFS_DISK_IMAGE_PATH
#define XFILE_FATFS_DISK_IMAGE_PATH  "fatfs_disk"
#endif

#ifndef XFILE_FATFS_DISK_IMAGE_SIZE
#define XFILE_FATFS_DISK_IMAGE_SIZE  (64LL * 1024 * 1024)  /* 64 MB */
#endif

/* ============================================================================
 * 内部状态
 * ============================================================================ */

static int* g_diskFds = NULL;         /* fd[FF_VOLUMES]，首次 initialize 时分配 */
static int  g_diskFdsInited = 0;
static uint32_t g_diskReadonlyMask = 0;

#if XFILE_FATFS_DISKIO_MODE == 1
/* 按逻辑序号枚举实际块设备，避免“存在 sdb 时索引仍按 sdb=1”导致卷号断档。 */
static bool findPhysicalDrive(int ordinal, char* outPath, size_t outSize)
{
    if (ordinal < 0 || !outPath || outSize == 0) return false;
    int current = 0;
    for (int i = 0; i < 26; ++i) {
        char path[64];
        struct stat st;
        snprintf(path, sizeof(path), "/dev/sd%c1", (char)('a' + i));
        if (stat(path, &st) == 0 && S_ISBLK(st.st_mode)) {
            if (current++ == ordinal) {
                snprintf(outPath, outSize, "%s", path);
                return true;
            }
        }
        snprintf(path, sizeof(path), "/dev/mmcblk%dp1", i);
        if (stat(path, &st) == 0 && S_ISBLK(st.st_mode)) {
            if (current++ == ordinal) {
                snprintf(outPath, outSize, "%s", path);
                return true;
            }
        }
    }
    return false;
}
#endif

static void ensureDiskFds(void)
{
    if (!g_diskFdsInited) {
        g_diskFds = (int*)XCalloc_System(FF_VOLUMES, sizeof(int));
        if (g_diskFds) {
            for (int i = 0; i < FF_VOLUMES; i++) {
                g_diskFds[i] = -1;
            }
        }
        g_diskFdsInited = 1;
    }
}

/* ============================================================================
 * disk_initialize - 初始化磁盘驱动器
 * ============================================================================ */

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv >= FF_VOLUMES) return STA_NOINIT | STA_NODISK;
    ensureDiskFds();
    if (!g_diskFds) return STA_NOINIT | STA_NODISK;

    if (g_diskFds[pdrv] >= 0) {
        close(g_diskFds[pdrv]);
        g_diskFds[pdrv] = -1;
    }
    g_diskReadonlyMask &= ~(1u << pdrv);

#if XFILE_FATFS_DISKIO_MODE == 0
    /* 文件镜像模式：动态生成文件名 fatfs_diskX.img */
    {
        char imgPath[256];
        const char* baseDir = getenv("XFILE_FATFS_DIR");
        if (baseDir && baseDir[0]) {
            snprintf(imgPath, sizeof(imgPath), "%s/" XFILE_FATFS_DISK_IMAGE_PATH "%d.img",
                     baseDir, pdrv);
        } else {
            snprintf(imgPath, sizeof(imgPath), XFILE_FATFS_DISK_IMAGE_PATH "%d.img", pdrv);
        }

        g_diskFds[pdrv] = open(imgPath, O_RDWR | O_CREAT, 0666);
        if (g_diskFds[pdrv] < 0) {
            return STA_NOINIT;
        }

        /* 新文件预分配镜像大小 */
        struct stat st;
        if (fstat(g_diskFds[pdrv], &st) == 0 && st.st_size == 0) {
#ifdef __linux__
            if (fallocate(g_diskFds[pdrv], 0, 0, XFILE_FATFS_DISK_IMAGE_SIZE) != 0) {
                if (ftruncate(g_diskFds[pdrv], XFILE_FATFS_DISK_IMAGE_SIZE) != 0) {
                    close(g_diskFds[pdrv]);
                    g_diskFds[pdrv] = -1;
                    return STA_NOINIT;
                }
            }
#else
            if (ftruncate(g_diskFds[pdrv], XFILE_FATFS_DISK_IMAGE_SIZE) != 0) {
                close(g_diskFds[pdrv]);
                g_diskFds[pdrv] = -1;
                return STA_NOINIT;
            }
#endif
            lseek(g_diskFds[pdrv], 0, SEEK_SET);
        }
    }
#else
    /* 物理磁盘模式：访问块设备 */
    {
        char devPath[64];
        if (!findPhysicalDrive((int)pdrv, devPath, sizeof(devPath))) {
            return STA_NOINIT | STA_NODISK;
        }

        /* 先尝试读写打开 */
        g_diskFds[pdrv] = open(devPath, O_RDWR | O_CLOEXEC);
        if (g_diskFds[pdrv] < 0) {
            /* 尝试只读打开 */
            g_diskFds[pdrv] = open(devPath, O_RDONLY | O_CLOEXEC);
            if (g_diskFds[pdrv] < 0) {
                return STA_NOINIT | STA_NODISK;
            }
            g_diskReadonlyMask |= (1u << pdrv);
            return STA_PROTECT;
        }
    }
#endif

    return (g_diskReadonlyMask & (1u << pdrv)) ? STA_PROTECT : 0;
}

/* ============================================================================
 * disk_status - 获取磁盘状态
 * ============================================================================ */

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv >= FF_VOLUMES) return STA_NOINIT;
    if (!g_diskFds || g_diskFds[pdrv] < 0) {
        return STA_NOINIT | STA_NODISK;
    }
    return (g_diskReadonlyMask & (1u << pdrv)) ? STA_PROTECT : 0;
}

/* ============================================================================
 * 物理磁盘 I/O 辅助 —— 对齐缓冲区
 * ============================================================================ */

#if XFILE_FATFS_DISKIO_MODE == 1
static BYTE* g_alignedBuf = NULL;
static UINT  g_alignedBufSize = 0;

static BYTE* ensureAlignedBuf(UINT size)
{
    if (g_alignedBuf && g_alignedBufSize >= size) return g_alignedBuf;
    if (g_alignedBuf) { XFree_System(g_alignedBuf); g_alignedBuf = NULL; }
    void* ptr = NULL;
    if (posix_memalign(&ptr, 512, size) == 0) {
        g_alignedBuf = (BYTE*)ptr;
        g_alignedBufSize = size;
    }
    return g_alignedBuf;
}
#endif

/* ============================================================================
 * disk_read - 读取扇区
 * ============================================================================ */

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
    if (pdrv >= FF_VOLUMES) return RES_PARERR;
    if (!g_diskFds || g_diskFds[pdrv] < 0) return RES_NOTRDY;
    if (!buff || count == 0) return RES_PARERR;
    if (count > (UINT)(SIZE_MAX / 512u)) return RES_PARERR;

    UINT sectorSize = 512;
    off_t offset = (off_t)sector * sectorSize;
    ssize_t total = (ssize_t)count * sectorSize;

#if XFILE_FATFS_DISKIO_MODE == 1
    BYTE* readBuf = buff;
    if ((uintptr_t)buff & 0x1FF) {
        readBuf = ensureAlignedBuf((UINT)count * sectorSize);
        if (!readBuf) return RES_ERROR;
    }
    ssize_t n = pread(g_diskFds[pdrv], readBuf, (size_t)total, offset);
    if (n != total) return RES_ERROR;
    if (readBuf != buff) {
        memcpy(buff, readBuf, (size_t)total);
    }
#else
    ssize_t n = pread(g_diskFds[pdrv], buff, (size_t)total, offset);
    if (n != total) return RES_ERROR;
#endif

    return RES_OK;
}

/* ============================================================================
 * disk_write - 写入扇区
 * ============================================================================ */

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
    if (pdrv >= FF_VOLUMES) return RES_PARERR;
    if (!g_diskFds || g_diskFds[pdrv] < 0) return RES_NOTRDY;
    if (!buff || count == 0) return RES_PARERR;
    if (count > (UINT)(SIZE_MAX / 512u)) return RES_PARERR;
    if (disk_status(pdrv) & STA_PROTECT) return RES_WRPRT;

    UINT sectorSize = 512;
    off_t offset = (off_t)sector * sectorSize;
    ssize_t total = (ssize_t)count * sectorSize;

#if XFILE_FATFS_DISKIO_MODE == 1
    const BYTE* writeBuf = buff;
    if ((uintptr_t)buff & 0x1FF) {
        BYTE* aligned = ensureAlignedBuf((UINT)count * sectorSize);
        if (!aligned) return RES_ERROR;
        memcpy(aligned, buff, (size_t)count * sectorSize);
        writeBuf = aligned;
    }
    ssize_t n = pwrite(g_diskFds[pdrv], writeBuf, (size_t)total, offset);
    if (n != total) return RES_ERROR;
#else
    ssize_t n = pwrite(g_diskFds[pdrv], buff, (size_t)total, offset);
    if (n != total) return RES_ERROR;
#endif

    return RES_OK;
}

/* ============================================================================
 * disk_ioctl - 磁盘控制
 * ============================================================================ */

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff)
{
    if (pdrv >= FF_VOLUMES) return RES_PARERR;
    if (!g_diskFds || g_diskFds[pdrv] < 0) return RES_NOTRDY;

    switch (cmd) {
        case CTRL_SYNC:
            if (fsync(g_diskFds[pdrv]) != 0) return RES_ERROR;
            return RES_OK;

        case GET_SECTOR_COUNT: {
            if (!buff) return RES_PARERR;
            off_t size = lseek(g_diskFds[pdrv], 0, SEEK_END);
            if (size < 0) return RES_ERROR;
            lseek(g_diskFds[pdrv], 0, SEEK_SET);
#if XFILE_FATFS_DISKIO_MODE == 0
            size = XFILE_FATFS_DISK_IMAGE_SIZE;
#endif
            *(LBA_t*)buff = (LBA_t)(size / 512);
            return RES_OK;
        }

        case GET_SECTOR_SIZE:
            if (!buff) return RES_PARERR;
            *(WORD*)buff = 512;
            return RES_OK;

        case GET_BLOCK_SIZE:
            if (!buff) return RES_PARERR;
            *(DWORD*)buff = 1;
            return RES_OK;

        case CTRL_TRIM: {
#ifdef __linux__
            if (!buff) return RES_PARERR;
            LBA_t* range = (LBA_t*)buff;
            if (range[1] < range[0]) return RES_PARERR;
            off_t trimOffset = (off_t)range[0] * 512;
            off_t trimLength = (off_t)(range[1] - range[0] + 1) * 512;
            fallocate(g_diskFds[pdrv], FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                      trimOffset, trimLength);
#endif
            return RES_OK;
        }

        default:
            return RES_PARERR;
    }
}

/* ============================================================================
 * XFileSystem_Fatfs_platform.h 平台实现
 * ============================================================================ */

/* ---- 驱动器信息 ---- */

#if XFILE_FATFS_DISKIO_MODE == 0
/* 文件镜像模式：使用宏预定义的驱动器列表 */
static const char* g_fileDrivePrefixes[] = { XFILE_FATFS_FILEMODE_DRIVE_STRS };

int XFatfsDrives_count(void)
{
    return XFILE_FATFS_FILEMODE_DRIVE_COUNT;
}

bool XFatfsDrives_at(int index, XString* path)
{
    if (!path || index < 0 || index >= XFILE_FATFS_FILEMODE_DRIVE_COUNT) return false;
    XString_assign_utf8(path, g_fileDrivePrefixes[index]);
    return true;
}

int XFatfsDrives_prefixToIndex(const char* prefix)
{
    if (!prefix) return -1;
    size_t prefixLen = strlen(prefix);
    for (int i = 0; i < XFILE_FATFS_FILEMODE_DRIVE_COUNT; i++) {
        size_t driveLen = strlen(g_fileDrivePrefixes[i]);
        if (prefixLen >= driveLen && memcmp(prefix, g_fileDrivePrefixes[i], driveLen) == 0) {
            return i;
        }
    }
    return -1;
}

#else
/* 物理磁盘模式：枚举系统块设备 */

int XFatfsDrives_count(void)
{
    int count = 0;
    char path[64];
    while (findPhysicalDrive(count, path, sizeof(path))) count++;
    return count;
}

bool XFatfsDrives_at(int index, XString* path)
{
    if (!path || index < 0) return false;

    char devPath[64];
    if (findPhysicalDrive(index, devPath, sizeof(devPath))) {
        char drivePath[8];
        char letter = 'A';
        if (strncmp(devPath, "/dev/sd", 7) == 0) letter = (char)('A' + (devPath[7] - 'a'));
        else if (strncmp(devPath, "/dev/mmcblk", 11) == 0) {
            int n = atoi(devPath + 11);
            if (n >= 0 && n < 26) letter = (char)('A' + n);
        }
        snprintf(drivePath, sizeof(drivePath), "%c:", letter);
        XString_assign_utf8(path, drivePath);
        return true;
    }
    return false;
}

int XFatfsDrives_prefixToIndex(const char* prefix)
{
    if (!prefix) return -1;
    if (prefix[0] && prefix[1] == ':') {
        char letter = (prefix[0] >= 'a' && prefix[0] <= 'z') ? prefix[0] - 'a' + 'A' : prefix[0];
        if (letter >= 'A' && letter <= 'Z') {
            char devPath[64];
            for (int ordinal = 0; findPhysicalDrive(ordinal, devPath, sizeof(devPath)); ++ordinal) {
                char mapped = 'A';
                if (strncmp(devPath, "/dev/sd", 7) == 0) mapped = (char)('A' + (devPath[7] - 'a'));
                else if (strncmp(devPath, "/dev/mmcblk", 11) == 0) {
                    int n = atoi(devPath + 11);
                    if (n >= 0 && n < 26) mapped = (char)('A' + n);
                }
                if (mapped == letter) return ordinal;
            }
        }
    }
    return -1;
}
#endif

/* ---- 特殊路径 ---- */

bool XFatfsPath_home(XString* path)
{
    if (!path) return false;
#if XFILE_FATFS_DISKIO_MODE == 0
    XString_assign_utf8(path, XFILE_FATFS_HOME_PATH);
    return true;
#else
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (home) {
        XString_assign_utf8(path, home);
        return true;
    }
    XString_assign_utf8(path, "/root");
    return true;
#endif
}

bool XFatfsPath_root(XString* path)
{
    if (!path) return false;
#if XFILE_FATFS_DISKIO_MODE == 0
    XString_assign_utf8(path, XFILE_FATFS_ROOT_PATH);
    return true;
#else
    XString_assign_utf8(path, "/");
    return true;
#endif
}

bool XFatfsPath_current(XString* path)
{
    if (!path) return false;
#if XFILE_FATFS_DISKIO_MODE == 0
    XString_assign_utf8(path, g_fileDrivePrefixes[0]);
    return true;
#else
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        XString_assign_utf8(path, cwd);
        return true;
    }
    return false;
#endif
}

bool XFatfsPath_setCurrent(const XString* path)
{
    if (!path) return false;
#if XFILE_FATFS_DISKIO_MODE == 0
    const char* utf8 = XString_toUtf8(path);
    if (!utf8) return false;
    int index = XFatfsDrives_prefixToIndex(utf8);
    const char* rest = utf8;
    if (index >= 0) {
        const char* colon = strchr(utf8, ':');
        rest = colon ? colon + 1 : utf8;
    } else {
        index = 0;
    }
    char fatfsPath[512];
    int n = snprintf(fatfsPath, sizeof(fatfsPath), "%d:%s", index, rest);
    if (n < 0 || (size_t)n >= sizeof(fatfsPath)) return false;
    return f_chdir(fatfsPath) == FR_OK;
#else
    const char* utf8 = XString_toUtf8(path);
    if (!utf8) return false;
    return (chdir(utf8) == 0);
#endif
}

bool XFatfsPath_temp(XString* path)
{
    if (!path) return false;
#if XFILE_FATFS_DISKIO_MODE == 0
    XString_assign_utf8(path, XFILE_FATFS_TEMP_PATH);
    return true;
#else
    const char* tmp = getenv("TMPDIR");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = getenv("TEMPDIR");
    if (!tmp) tmp = "/tmp";
    XString_assign_utf8(path, tmp);
    return true;
#endif
}

#endif /* XFILE_USE_FATFS */

#endif /* __linux__ || __APPLE__ || __BSD__ */
