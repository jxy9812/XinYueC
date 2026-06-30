#ifdef _WIN32

#include "XFileSystem_config.h"

/* 仅在启用FatFS模式时编译 */
#if defined(XFILE_USE_FATFS)

#include "ff.h"           /* LBA_t 等类型定义 */
#include "diskio.h"
#include <windows.h>
#include <stdio.h>

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
 * 内部状态
 * ============================================================================ */

#if XFILE_FATFS_DISKIO_MODE == 0
    /* 文件镜像模式：每个驱动器号对应一个 img 文件 */
    static HANDLE g_diskHandles[FF_VOLUMES] = {INVALID_HANDLE_VALUE};
    static const char* g_imagePaths[FF_VOLUMES] = {
        XFILE_FATFS_DISK_IMAGE_PATH,
        XFILE_FATFS_DISK_IMAGE_PATH "2",
        XFILE_FATFS_DISK_IMAGE_PATH "3"
    };
#else
    /* 物理磁盘模式 */
    static HANDLE g_diskHandles[FF_VOLUMES] = {INVALID_HANDLE_VALUE};
#endif

/* ============================================================================
 * disk_initialize - 初始化磁盘驱动器
 * ============================================================================ */

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv >= FF_VOLUMES) return STA_NOINIT | STA_NODISK;

#if XFILE_FATFS_DISKIO_MODE == 0
    /* 文件镜像模式：打开 img 文件 */
    if (g_diskHandles[pdrv] != INVALID_HANDLE_VALUE) {
        CloseHandle(g_diskHandles[pdrv]);
    }

    g_diskHandles[pdrv] = CreateFileA(
        g_imagePaths[pdrv],
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (g_diskHandles[pdrv] == INVALID_HANDLE_VALUE) {
        return STA_NOINIT | STA_NODISK;
    }
#else
    /* 物理磁盘模式 */
    if (g_diskHandles[pdrv] != INVALID_HANDLE_VALUE) {
        CloseHandle(g_diskHandles[pdrv]);
    }

    char drivePath[64];
    snprintf(drivePath, sizeof(drivePath), "\\\\.\\%c:", 'C' + pdrv);

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
#endif

    return 0;
}

/* ============================================================================
 * disk_status - 获取磁盘状态
 * ============================================================================ */

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv >= FF_VOLUMES) return STA_NOINIT;

    if (g_diskHandles[pdrv] == INVALID_HANDLE_VALUE) {
        return STA_NOINIT | STA_NODISK;
    }

    return 0;
}

/* ============================================================================
 * disk_read - 读取扇区
 * ============================================================================ */

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
    if (pdrv >= FF_VOLUMES || g_diskHandles[pdrv] == INVALID_HANDLE_VALUE) {
        return RES_PARERR;
    }

    LARGE_INTEGER offset;
    offset.QuadPart = (LONGLONG)sector * FF_MAX_SS;

    DWORD bytesRead;
    OVERLAPPED overlapped = {0};
    overlapped.Offset = offset.LowPart;
    overlapped.OffsetHigh = offset.HighPart;

    if (!ReadFile(g_diskHandles[pdrv], buff, count * FF_MAX_SS, &bytesRead, &overlapped)) {
        return RES_ERROR;
    }

    return RES_OK;
}

/* ============================================================================
 * disk_write - 写入扇区
 * ============================================================================ */

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
    if (pdrv >= FF_VOLUMES || g_diskHandles[pdrv] == INVALID_HANDLE_VALUE) {
        return RES_PARERR;
    }

#if XFILE_FATFS_DISKIO_MODE == 1
    /* 物理磁盘模式检查写保护 */
    DWORD status = disk_status(pdrv);
    if (status & STA_PROTECT) return RES_WRPRT;
#endif

    LARGE_INTEGER offset;
    offset.QuadPart = (LONGLONG)sector * FF_MAX_SS;

    DWORD bytesWritten;
    OVERLAPPED overlapped = {0};
    overlapped.Offset = offset.LowPart;
    overlapped.OffsetHigh = offset.HighPart;

    if (!WriteFile(g_diskHandles[pdrv], buff, count * FF_MAX_SS, &bytesWritten, &overlapped)) {
        return RES_ERROR;
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

#endif /* XFILE_USE_FATFS */

#endif /* _WIN32 */