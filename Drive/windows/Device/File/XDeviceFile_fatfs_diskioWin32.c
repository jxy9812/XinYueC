#ifdef _WIN32

#include "XFileSystem_config.h"

/* 仅在启用FatFS模式时编译 */
#if defined(XFILE_USE_FATFS)

#include "ff.h"           /* LBA_t 等类型定义 */
#include "diskio.h"
#include "XDeviceFile.h"
#include "XString.h"
#include "XMemory.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include <shlobj.h>

/* ============================================================================
 * 磁盘I/O模式配置
 * ============================================================================ */

#ifndef XFILE_FATFS_DISKIO_MODE
#define XFILE_FATFS_DISKIO_MODE       0    /* 0=文件镜像, 1=物理磁盘 */
#endif

#ifndef XFILE_FATFS_DISK_IMAGE_PATH
#define XFILE_FATFS_DISK_IMAGE_PATH   "fatfs_disk"
#endif

/* ============================================================================
 * 内部状态（动态分配，适配 FF_VOLUMES=10）
 * ============================================================================ */

static HANDLE* g_diskHandles = NULL;       /* HANDLE[FF_VOLUMES]，首次 initialize 时分配 */
static int g_diskHandlesInited = 0;
static uint32_t g_diskReadonlyMask = 0;

#if XFILE_FATFS_DISKIO_MODE == 1
static bool findLogicalDrive(int ordinal, char* outPath, size_t outSize)
{
    if (ordinal < 0 || !outPath || outSize == 0) return false;
    DWORD mask = GetLogicalDrives();
    int current = 0;
    for (int i = 0; i < 26; ++i) {
        if (mask & (1u << i)) {
            if (current++ == ordinal) {
                snprintf(outPath, outSize, "\\\\.\\%c:", (char)('A' + i));
                return true;
            }
        }
    }
    return false;
}
#endif

static void ensureDiskHandles(void)
{
    if (!g_diskHandlesInited) {
        g_diskHandles = (HANDLE*)XMalloc_System(FF_VOLUMES * sizeof(HANDLE));
        if (g_diskHandles) {
            for (int i = 0; i < FF_VOLUMES; i++) {
                g_diskHandles[i] = INVALID_HANDLE_VALUE;
            }
        }
        g_diskHandlesInited = 1;
    }
}

/* ============================================================================
 * disk_initialize - 初始化磁盘驱动器
 * ============================================================================ */

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv >= FF_VOLUMES) return STA_NOINIT | STA_NODISK;
    ensureDiskHandles();
    if (!g_diskHandles) return STA_NOINIT | STA_NODISK;

    if (g_diskHandles[pdrv] != INVALID_HANDLE_VALUE) {
        CloseHandle(g_diskHandles[pdrv]);
    }
    g_diskReadonlyMask &= ~(1u << pdrv);

#if XFILE_FATFS_DISKIO_MODE == 0
    /* 文件镜像模式：动态生成文件名 fatfs_diskX.img */
    {
        char imgPath[MAX_PATH];
        char baseDir[MAX_PATH];
        DWORD baseLen = GetEnvironmentVariableA("XFILE_FATFS_DIR", baseDir, sizeof(baseDir));
        if (baseLen > 0 && baseLen < sizeof(baseDir)) {
            snprintf(imgPath, sizeof(imgPath), "%s\\%s%d.img", baseDir,
                     XFILE_FATFS_DISK_IMAGE_PATH, pdrv);
        } else {
            snprintf(imgPath, sizeof(imgPath), XFILE_FATFS_DISK_IMAGE_PATH "%d.img", pdrv);
        }
        g_diskHandles[pdrv] = CreateFileA(
            imgPath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (g_diskHandles[pdrv] != INVALID_HANDLE_VALUE) {
            /* 新文件预分配镜像大小（由宏 XFILE_FATFS_DISK_IMAGE_SIZE 控制） */
            LARGE_INTEGER fileSize;
            if (!GetFileSizeEx(g_diskHandles[pdrv], &fileSize)) {
                CloseHandle(g_diskHandles[pdrv]);
                g_diskHandles[pdrv] = INVALID_HANDLE_VALUE;
                return STA_NOINIT;
            }
            if (fileSize.QuadPart == 0) {
                LARGE_INTEGER newSize;
                newSize.QuadPart = XFILE_FATFS_DISK_IMAGE_SIZE;
                if (!SetFilePointerEx(g_diskHandles[pdrv], newSize, NULL, FILE_BEGIN)
                    || !SetEndOfFile(g_diskHandles[pdrv])) {
                    CloseHandle(g_diskHandles[pdrv]);
                    g_diskHandles[pdrv] = INVALID_HANDLE_VALUE;
                    return STA_NOINIT;
                }
            }
            SetFilePointer(g_diskHandles[pdrv], 0, NULL, FILE_BEGIN);
        } else {
            return STA_NOINIT;
        }
    }
#else
    /* 物理磁盘模式 */
    {
        char drivePath[64];
        if (!findLogicalDrive((int)pdrv, drivePath, sizeof(drivePath))) {
            return STA_NOINIT | STA_NODISK;
        }

        g_diskHandles[pdrv] = CreateFileA(
            drivePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,  /* 去掉 FILE_FLAG_NO_BUFFERING，避免对齐问题 */
            NULL
        );

        if (g_diskHandles[pdrv] == INVALID_HANDLE_VALUE) {
            /* 尝试只读打开 */
            g_diskHandles[pdrv] = CreateFileA(
                drivePath,
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            if (g_diskHandles[pdrv] == INVALID_HANDLE_VALUE) {
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
    if (!g_diskHandles || g_diskHandles[pdrv] == INVALID_HANDLE_VALUE) {
        return STA_NOINIT | STA_NODISK;
    }

    return (g_diskReadonlyMask & (1u << pdrv)) ? STA_PROTECT : 0;
}

/* ============================================================================
 * 物理磁盘 I/O 辅助 —— 分配扇区对齐缓冲区
 * ============================================================================ */

#if XFILE_FATFS_DISKIO_MODE == 1
/* 每个逻辑卷独占对齐缓冲区，避免不同卷并发读写时互相覆盖。 */
static BYTE* g_alignedBuf[FF_VOLUMES] = { 0 };
static UINT  g_alignedBufSize[FF_VOLUMES] = { 0 };

static BYTE* ensureAlignedBuf(BYTE pdrv, UINT size)
{
    if (pdrv >= FF_VOLUMES) return NULL;
    if (g_alignedBuf[pdrv] && g_alignedBufSize[pdrv] >= size) return g_alignedBuf[pdrv];
    if (g_alignedBuf[pdrv]) {
        XFree_System(g_alignedBuf[pdrv]);
        g_alignedBuf[pdrv] = NULL;
        g_alignedBufSize[pdrv] = 0;
    }
    /* 使用 XinYueC 对齐分配器，保证释放函数与分配函数匹配。 */
    g_alignedBuf[pdrv] = (BYTE*)XAlignedMalloc_System(size, FF_MAX_SS);
    if (g_alignedBuf[pdrv]) g_alignedBufSize[pdrv] = size;
    return g_alignedBuf[pdrv];
}
#endif

/* ============================================================================
 * disk_read - 读取扇区
 * ============================================================================ */

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
    if (pdrv >= FF_VOLUMES || !g_diskHandles || g_diskHandles[pdrv] == INVALID_HANDLE_VALUE
        || !buff || count == 0 || count > (UINT)(MAXDWORD / FF_MAX_SS)) {
        return RES_PARERR;
    }
#if XFILE_FATFS_DISKIO_MODE == 1
    /* 物理磁盘模式：同步句柄配合对齐缓冲区，避免未开启 OVERLAPPED 时
     * 传入 OVERLAPPED 结构导致 ERROR_INVALID_PARAMETER。 */
    {
        UINT size = count * FF_MAX_SS;
        BYTE* alignedBuf = ensureAlignedBuf(pdrv, size);
        if (!alignedBuf) return RES_ERROR;

        LARGE_INTEGER offset;
        offset.QuadPart = (LONGLONG)sector * FF_MAX_SS;
        if (!SetFilePointerEx(g_diskHandles[pdrv], offset, NULL, FILE_BEGIN)) return RES_ERROR;

        DWORD bytesRead;
        if (!ReadFile(g_diskHandles[pdrv], alignedBuf, size, &bytesRead, NULL)) {
            return RES_ERROR;
        }
        if (bytesRead < size) {
            memset(alignedBuf + bytesRead, 0, size - bytesRead);
        }
        memcpy(buff, alignedBuf, size);
    }
#else
    /* 文件镜像模式：顺序 I/O */
    {
        LARGE_INTEGER offset;
        offset.QuadPart = (LONGLONG)sector * FF_MAX_SS;
        if (!SetFilePointerEx(g_diskHandles[pdrv], offset, NULL, FILE_BEGIN)) return RES_ERROR;

        DWORD bytesRead;
        if (!ReadFile(g_diskHandles[pdrv], buff, count * FF_MAX_SS, &bytesRead, NULL)) return RES_ERROR;
        if (bytesRead < count * FF_MAX_SS) {
            memset(buff + bytesRead, 0, count * FF_MAX_SS - bytesRead);
        }
    }
#endif

    return RES_OK;
}

/* ============================================================================
 * disk_write - 写入扇区
 * ============================================================================ */

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
    if (pdrv >= FF_VOLUMES || !g_diskHandles || g_diskHandles[pdrv] == INVALID_HANDLE_VALUE
        || !buff || count == 0 || count > (UINT)(MAXDWORD / FF_MAX_SS)) {
        return RES_PARERR;
    }
    if (disk_status(pdrv) & STA_PROTECT) return RES_WRPRT;

#if XFILE_FATFS_DISKIO_MODE == 1
    /* 物理磁盘模式：同步句柄配合对齐缓冲区。 */
    {
        DWORD status = disk_status(pdrv);
        if (status & STA_PROTECT) return RES_WRPRT;

        UINT size = count * FF_MAX_SS;
        BYTE* alignedBuf = ensureAlignedBuf(pdrv, size);
        if (!alignedBuf) return RES_ERROR;
        memcpy(alignedBuf, buff, size);

        LARGE_INTEGER offset;
        offset.QuadPart = (LONGLONG)sector * FF_MAX_SS;
        if (!SetFilePointerEx(g_diskHandles[pdrv], offset, NULL, FILE_BEGIN)) return RES_ERROR;

        DWORD bytesWritten;
        if (!WriteFile(g_diskHandles[pdrv], alignedBuf, size, &bytesWritten, NULL)) {
            return RES_ERROR;
        }
        if (bytesWritten < size) return RES_ERROR;
    }
#else
    /* 文件镜像模式：顺序 I/O */
    {
        LARGE_INTEGER offset;
        offset.QuadPart = (LONGLONG)sector * FF_MAX_SS;
        if (!SetFilePointerEx(g_diskHandles[pdrv], offset, NULL, FILE_BEGIN)) return RES_ERROR;

        DWORD bytesWritten;
        if (!WriteFile(g_diskHandles[pdrv], buff, count * FF_MAX_SS, &bytesWritten, NULL)) return RES_ERROR;
        if (bytesWritten < count * FF_MAX_SS) {
            LARGE_INTEGER newEnd;
            newEnd.QuadPart = offset.QuadPart + count * FF_MAX_SS;
            if (!SetFilePointerEx(g_diskHandles[pdrv], newEnd, NULL, FILE_BEGIN)
                || !SetEndOfFile(g_diskHandles[pdrv])) return RES_ERROR;
            SetFilePointerEx(g_diskHandles[pdrv], offset, NULL, FILE_BEGIN);
            if (!WriteFile(g_diskHandles[pdrv], buff, count * FF_MAX_SS, &bytesWritten, NULL)) return RES_ERROR;
        }
    }
#endif

    return RES_OK;
}

/* ============================================================================
 * disk_ioctl - 磁盘控制
 * ============================================================================ */

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff)
{
    if (pdrv >= FF_VOLUMES || !g_diskHandles || g_diskHandles[pdrv] == INVALID_HANDLE_VALUE) {
        return RES_PARERR;
    }

    switch (cmd) {
        case CTRL_SYNC:
            FlushFileBuffers(g_diskHandles[pdrv]);
            return RES_OK;

        case GET_SECTOR_COUNT: {
            if (!buff) return RES_PARERR;
            LARGE_INTEGER fileSize;
            if (!GetFileSizeEx(g_diskHandles[pdrv], &fileSize)) return RES_ERROR;
            *(DWORD*)buff = (DWORD)(fileSize.QuadPart / FF_MAX_SS);
            return RES_OK;
        }

        case GET_SECTOR_SIZE:
            if (!buff) return RES_PARERR;
            *(DWORD*)buff = FF_MAX_SS;
            return RES_OK;

        case GET_BLOCK_SIZE:
            if (!buff) return RES_PARERR;
            *(DWORD*)buff = 1;  /* 擦除块大小（1扇区） */
            return RES_OK;

        default:
            return RES_PARERR;
    }
}

/* ============================================================================
 * FatFs 平台内部适配实现
 * ============================================================================ */

/* ---- 驱动器信息 ---- */

#if XFILE_FATFS_DISKIO_MODE == 0
/* 文件镜像模式：使用宏预定义的驱动器列表 */
static const char* g_fileDrivePrefixes[] = { XFILE_FATFS_FILEMODE_DRIVE_STRS };

int XFATFS_platformDriveCount(void)
{
    return XFILE_FATFS_FILEMODE_DRIVE_COUNT;
}

bool XFATFS_platformDriveAt(int index, XString* path)
{
    if (!path || index < 0 || index >= XFILE_FATFS_FILEMODE_DRIVE_COUNT) return false;
    XString_assign_utf8(path, g_fileDrivePrefixes[index]);
    return true;
}

int XFATFS_platformDrivePrefixToIndex(const char* prefix)
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
/* 物理磁盘模式：枚举系统逻辑驱动器 */

int XFATFS_platformDriveCount(void)
{
    DWORD mask = GetLogicalDrives();
    int count = 0;
    for (int i = 0; i < 26; i++) if (mask & (1u << i)) ++count;
    return count;
}

bool XFATFS_platformDriveAt(int index, XString* path)
{
    if (!path || index < 0) return false;

    DWORD mask = GetLogicalDrives();
    int count = 0;
    for (int i = 0; i < 26; i++) {
        if (mask & (1u << i) && count++ == index) {
            char drivePath[4] = { (char)('A' + i), ':', '\\', '\0' };
            XString_assign_utf8(path, drivePath);
            return true;
        }
    }
    return false;
}

int XFATFS_platformDrivePrefixToIndex(const char* prefix)
{
    if (!prefix) return -1;
    /* 物理磁盘模式：匹配单字母+冒号前缀（如 "C:", "C:/", "D:\"） */
    if (prefix[0] && prefix[1] == ':') {
        char letter = (prefix[0] >= 'a' && prefix[0] <= 'z') ? prefix[0] - 'a' + 'A' : prefix[0];
        if (letter >= 'A' && letter <= 'Z') {
            DWORD mask = GetLogicalDrives();
            int ordinal = 0;
            for (int i = 0; i < 26; ++i) {
                if (mask & (1u << i)) {
                    if (i == letter - 'A') return ordinal;
                    ++ordinal;
                }
            }
        }
    }
    return -1;
}
#endif

/* ---- 特殊路径 ---- */

bool XFATFS_platformHomePath(XString* path)
{
    if (!path) return false;
#if XFILE_FATFS_DISKIO_MODE == 0
    XString_assign_utf8(path, XFILE_FATFS_HOME_PATH);
    return true;
#else
    wchar_t wPath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, wPath) != S_OK) return false;

    char utf8Path[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, wPath, -1, utf8Path, MAX_PATH, NULL, NULL);
    XString_assign_utf8(path, utf8Path);
    return true;
#endif
}

bool XFATFS_platformRootPath(XString* path)
{
    if (!path) return false;
#if XFILE_FATFS_DISKIO_MODE == 0
    XString_assign_utf8(path, XFILE_FATFS_ROOT_PATH);
    return true;
#else
    wchar_t wPath[MAX_PATH];
    if (GetCurrentDirectoryW(MAX_PATH, wPath) == 0) {
        XString_assign_utf8(path, "C:\\");
        return true;
    }
    char utf8Path[4];
    utf8Path[0] = (char)wPath[0];
    utf8Path[1] = ':';
    utf8Path[2] = '\\';
    utf8Path[3] = '\0';
    XString_assign_utf8(path, utf8Path);
    return true;
#endif
}

bool XFATFS_platformCurrentPath(XString* path)
{
    if (!path) return false;
#if XFILE_FATFS_DISKIO_MODE == 0
    XString_assign_utf8(path, g_fileDrivePrefixes[0]);  /* 默认第一个驱动器 */
    return true;
#else
    wchar_t wPath[MAX_PATH];
    if (GetCurrentDirectoryW(MAX_PATH, wPath) == 0) return false;

    char utf8Path[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, wPath, -1, utf8Path, MAX_PATH, NULL, NULL);
    XString_assign_utf8(path, utf8Path);
    return true;
#endif
}

bool XFATFS_platformSetCurrentPath(const XString* path)
{
    if (!path) return false;
#if XFILE_FATFS_DISKIO_MODE == 0
    const char* utf8 = XString_toUtf8(path);
    if (!utf8) return false;
    int index = XFATFS_platformDrivePrefixToIndex(utf8);
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

    wchar_t wPath[MAX_PATH];
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wPath, MAX_PATH) == 0) return false;

    return SetCurrentDirectoryW(wPath) != 0;
#endif
}

bool XFATFS_platformTempPath(XString* path)
{
    if (!path) return false;
#if XFILE_FATFS_DISKIO_MODE == 0
    XString_assign_utf8(path, XFILE_FATFS_TEMP_PATH);
    return true;
#else
    wchar_t wTempPath[MAX_PATH];
    DWORD len = GetTempPathW(MAX_PATH, wTempPath);
    if (len == 0 || len >= MAX_PATH) return false;

    char utf8Path[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, wTempPath, -1, utf8Path, MAX_PATH, NULL, NULL);
    XString_assign_utf8(path, utf8Path);
    return true;
#endif
}

#endif /* XFILE_USE_FATFS */

#endif /* _WIN32 */
