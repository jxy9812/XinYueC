#ifdef _WIN32

#include "XFileSystem_config.h"

/* 仅在启用FatFS模式时编译 */
#if defined(XFILE_USE_FATFS)

#include "ff.h"           /* LBA_t 等类型定义 */
#include "diskio.h"
#include "XFileSystem_Fatfs_platform.h"
#include "XString.h"
#include <windows.h>
#include <shlobj.h>

/* ============================================================================
 * 磁盘I/O模式配置
 * ============================================================================ */

#ifndef XFILE_FATFS_DISKIO_MODE
#define XFILE_FATFS_DISKIO_MODE       0    /* 0=文件镜像, 1=物理磁盘 */
#endif

#ifndef XFILE_FATFS_DISK_IMAGE_PATH
#define XFILE_FATFS_DISK_IMAGE_PATH   "fatfs_disk.img"
#endif

#ifndef XFILE_FATFS_DISK_PHYSICAL_DRIVE
#define XFILE_FATFS_DISK_PHYSICAL_DRIVE "\\\\.\\D:"
#endif

/* ============================================================================
 * 内部状态（动态分配，适配 FF_VOLUMES=26）
 * ============================================================================ */

static HANDLE* g_diskHandles = NULL;       /* HANDLE[FF_VOLUMES]，首次 initialize 时分配 */
static int g_diskHandlesInited = 0;

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

#if XFILE_FATFS_DISKIO_MODE == 0
    /* 文件镜像模式：动态生成文件名 fatfs_diskX.img */
    {
        char imgPath[64];
        snprintf(imgPath, sizeof(imgPath), XFILE_FATFS_DISK_IMAGE_PATH "%d", pdrv);
        g_diskHandles[pdrv] = CreateFileA(
            imgPath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (g_diskHandles[pdrv] != INVALID_HANDLE_VALUE) {
            /* 新文件预分配镜像大小（由宏 XFILE_FATFS_DISK_IMAGE_SIZE 控制） */
            LARGE_INTEGER fileSize;
            if (GetFileSizeEx(g_diskHandles[pdrv], &fileSize) && fileSize.QuadPart == 0) {
                LARGE_INTEGER newSize;
                newSize.QuadPart = XFILE_FATFS_DISK_IMAGE_SIZE;
                SetFilePointerEx(g_diskHandles[pdrv], newSize, NULL, FILE_BEGIN);
                SetEndOfFile(g_diskHandles[pdrv]);
                SetFilePointerEx(g_diskHandles[pdrv], (LARGE_INTEGER){0}, NULL, FILE_BEGIN);
            }
        }
    }
#else
    /* 物理磁盘模式 */
    {
        char drivePath[64];
        snprintf(drivePath, sizeof(drivePath), "\\\\.\\%c:", 'A' + pdrv);

        g_diskHandles[pdrv] = CreateFileA(
            drivePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_NO_BUFFERING,
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
                FILE_FLAG_NO_BUFFERING,
                NULL
            );
            if (g_diskHandles[pdrv] == INVALID_HANDLE_VALUE) {
                return STA_NOINIT | STA_NODISK;
            }
            return STA_PROTECT;
        }
    }
#endif

    return 0;
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

    return 0;
}

/* ============================================================================
 * disk_read - 读取扇区
 * ============================================================================ */

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
    if (pdrv >= FF_VOLUMES || !g_diskHandles || g_diskHandles[pdrv] == INVALID_HANDLE_VALUE) {
        return RES_PARERR;
    }

    LARGE_INTEGER offset;
    offset.QuadPart = (LONGLONG)sector * FF_MAX_SS;
    if (!SetFilePointerEx(g_diskHandles[pdrv], offset, NULL, FILE_BEGIN)) {
        return RES_ERROR;
    }

    DWORD bytesRead;
    if (!ReadFile(g_diskHandles[pdrv], buff, count * FF_MAX_SS, &bytesRead, NULL)) {
        return RES_ERROR;
    }
    /* 读取不足的部分用零填充（空文件或文件末尾） */
    if (bytesRead < count * FF_MAX_SS) {
        memset(buff + bytesRead, 0, count * FF_MAX_SS - bytesRead);
    }

    return RES_OK;
}

/* ============================================================================
 * disk_write - 写入扇区
 * ============================================================================ */

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
    if (pdrv >= FF_VOLUMES || !g_diskHandles || g_diskHandles[pdrv] == INVALID_HANDLE_VALUE) {
        return RES_PARERR;
    }

#if XFILE_FATFS_DISKIO_MODE == 1
    /* 物理磁盘模式检查写保护 */
    DWORD status = disk_status(pdrv);
    if (status & STA_PROTECT) return RES_WRPRT;
#endif

    LARGE_INTEGER offset;
    offset.QuadPart = (LONGLONG)sector * FF_MAX_SS;
    if (!SetFilePointerEx(g_diskHandles[pdrv], offset, NULL, FILE_BEGIN)) {
        return RES_ERROR;
    }

    DWORD bytesWritten;
    if (!WriteFile(g_diskHandles[pdrv], buff, count * FF_MAX_SS, &bytesWritten, NULL)) {
        return RES_ERROR;
    }
    /* 写入不足时扩展文件（稀疏文件扩展） */
    if (bytesWritten < count * FF_MAX_SS) {
        LARGE_INTEGER newEnd;
        newEnd.QuadPart = offset.QuadPart + count * FF_MAX_SS;
        if (!SetFilePointerEx(g_diskHandles[pdrv], newEnd, NULL, FILE_BEGIN)
            || !SetEndOfFile(g_diskHandles[pdrv])) {
            return RES_ERROR;
        }
        /* 重新定位到写入位置并重试 */
        if (!SetFilePointerEx(g_diskHandles[pdrv], offset, NULL, FILE_BEGIN)
            || !WriteFile(g_diskHandles[pdrv], buff, count * FF_MAX_SS, &bytesWritten, NULL)) {
            return RES_ERROR;
        }
    }

    return RES_OK;
}

/* ============================================================================
 * disk_ioctl - 磁盘控制
 * ============================================================================ */

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff)
{
    if (pdrv >= FF_VOLUMES || g_diskHandles[pdrv] == INVALID_HANDLE_VALUE) {
        return RES_PARERR;
    }

    switch (cmd) {
        case CTRL_SYNC:
            FlushFileBuffers(g_diskHandles[pdrv]);
            return RES_OK;

        case GET_SECTOR_COUNT: {
            LARGE_INTEGER fileSize;
            if (!GetFileSizeEx(g_diskHandles[pdrv], &fileSize)) return RES_ERROR;
            *(DWORD*)buff = (DWORD)(fileSize.QuadPart / FF_MAX_SS);
            return RES_OK;
        }

        case GET_SECTOR_SIZE:
            *(DWORD*)buff = FF_MAX_SS;
            return RES_OK;

        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 1;  /* 擦除块大小（1扇区） */
            return RES_OK;

        default:
            return RES_PARERR;
    }
}

/* ============================================================================
 * XFileSystem_Fatfs_platform.h 平台实现（5个）
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

#else
/* 物理磁盘模式：枚举系统逻辑驱动器 */

int XFatfsDrives_count(void)
{
    DWORD mask = GetLogicalDrives();
    int count = 0;
    for (int i = 0; i < 26; i++) {
        if (mask & (1 << i)) count++;
    }
    return count;
}

bool XFatfsDrives_at(int index, XString* path)
{
    if (!path || index < 0) return false;

    DWORD mask = GetLogicalDrives();
    int count = 0;
    for (int i = 0; i < 26; i++) {
        if (mask & (1 << i)) {
            if (count == index) {
                char drivePath[4];
                drivePath[0] = 'A' + i;
                drivePath[1] = ':';
                drivePath[2] = '\\';
                drivePath[3] = '\0';
                XString_assign_utf8(path, drivePath);
                return true;
            }
            count++;
        }
    }
    return false;
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
    wchar_t wPath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, wPath) != S_OK) return false;

    char utf8Path[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, wPath, -1, utf8Path, MAX_PATH, NULL, NULL);
    XString_assign_utf8(path, utf8Path);
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

bool XFatfsPath_current(XString* path)
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

bool XFatfsPath_setCurrent(const XString* path)
{
    if (!path) return false;
#if XFILE_FATFS_DISKIO_MODE == 0
    /* 文件镜像模式：由 FatFS 的 f_chdir 管理，平台层返回 true */
    return true;
#else
    const char* utf8 = XString_toUtf8(path);
    if (!utf8) return false;

    wchar_t wPath[MAX_PATH];
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wPath, MAX_PATH) == 0) return false;

    return SetCurrentDirectoryW(wPath) != 0;
#endif
}

bool XFatfsPath_temp(XString* path)
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
