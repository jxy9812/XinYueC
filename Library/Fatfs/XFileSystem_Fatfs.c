#include "XFileSystem_config.h"

/* 仅在启用FatFS模式时编译此文件 */
#if defined(XFILE_USE_FATFS)

#include "XFileSystem.h"
#include "XFileSystem_Fatfs_platform.h"  /* 平台抽象层 */
#include "XFileDevice.h"
#include "XMemory.h"
#include "XString.h"
#include "XDateTime.h"        /* XDateTime_currentDateTime 等 */
#include "XTypes.h"           /* XFd, XFD_INVALID */
#include "XFileDescriptor.h"  /* XFd_alloc, XFd_free, XFd_handle */
#include "ff.h"               /* FatFs API */
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * RTC 函数 - get_fattime（Fatfs 要求的实时时钟，跨平台通用）
 * ============================================================================ */

DWORD get_fattime(void)
{
    XDateTime now = XDateTime_currentDateTime();
    XDate date = XDateTime_date(&now);
    XTime time = XDateTime_time(&now);
    
    return (DWORD)((XDate_year(&date) - 1980) << 25)
         | (DWORD)(XDate_month(&date) << 21)
         | (DWORD)(XDate_day(&date) << 16)
         | (DWORD)(XTime_hour(&time) << 11)
         | (DWORD)(XTime_minute(&time) << 5)
         | (DWORD)(XTime_second(&time) / 2);
}

/* ============================================================================
 * 句柄映射表 - XFd 通过全局 XFileDescriptor 表管理，handle 存 FIL*
 * ============================================================================ */

/* 文件句柄包装：FIL + 路径（供 fstat/setFileTime 使用 f_stat/f_utime） */
typedef struct XFATFS_FileHandle {
    FIL  fil;            /* FatFs 文件对象 */
    char path[512];      /* FatFs 格式路径（供 fstat/setFileTime 使用） */
} XFATFS_FileHandle;

/* 目录句柄包装：DIR + FILINFO */
typedef struct XFATFS_DirHandle {
    DIR     dir;
    FILINFO info;
} XFATFS_DirHandle;

/* ============================================================================
 * 卷管理
 * ============================================================================ */

/* 动态卷管理：指针数组 + 位掩码（26×8 + 4 = 212 字节，vs 原 26×~1000 = 26KB） */
#define XFATFS_MAX_VOLUMES 26

static FATFS* g_volumes[XFATFS_MAX_VOLUMES];   /* NULL = 未分配 */
static uint32_t g_mountedMask = 0;              /* bit[i] = 已挂载 */
static int g_defaultIndex = -1;                 /* 默认驱动器索引 */

/** 获取或分配指定索引的 FATFS* */
static FATFS* XFATFS_getFs(int idx)
{
    if (idx < 0 || idx >= XFATFS_MAX_VOLUMES) return NULL;
    if (!g_volumes[idx]) {
        g_volumes[idx] = (FATFS*)XMalloc_System(sizeof(FATFS));
        if (g_volumes[idx]) memset(g_volumes[idx], 0, sizeof(FATFS));
    }
    return g_volumes[idx];
}

/**
 * @brief 从路径中提取卷标，返回Fatfs驱动器号（TCHAR格式的字符串编号）
 * @param utf8Path UTF-8路径（如 "C:/file.txt"）
 * @param outPath 输出去掉卷标前缀的路径
 * @return Fatfs驱动器字符串编号（"0:", "1:", "2:"），失败返回NULL
 */
static const char* XFATFS_parseVolume(const char* utf8Path, const char** outPath)
{
    /* 通过平台函数在驱动器前缀列表中查找索引 */
    int bestIdx = XFatfsDrives_prefixToIndex(utf8Path);
    if (bestIdx >= 0) {
        /* 获取匹配的前缀长度（如 "C:"=2, "sd:"=3） */
        XString* xDrive = XString_create();
        if (!xDrive) return NULL;
        const char* driveUtf8 = NULL;
        size_t bestLen = 0;
        if (XFatfsDrives_at(bestIdx, xDrive)) {
            driveUtf8 = XString_toUtf8(xDrive);
            if (driveUtf8) bestLen = strlen(driveUtf8);
        }
        XString_delete_base(xDrive);
        if (!driveUtf8 || bestLen == 0) return NULL;

        *outPath = utf8Path + bestLen;
        while (**outPath == '/' || **outPath == '\\') (*outPath)++;

        FATFS* fs = XFATFS_getFs(bestIdx);
        if (!fs) return NULL;

        if (g_defaultIndex < 0) g_defaultIndex = bestIdx;

        if (!(g_mountedMask & (1u << bestIdx))) {
            char mountStr[4];
            mountStr[0] = '0' + bestIdx;
            mountStr[1] = ':';
            mountStr[2] = '\0';

            FRESULT fr = f_mount(fs, mountStr, 1);
#if XFILE_FATFS_DISKIO_MODE == 0
            if (fr == FR_NO_FILESYSTEM) {
                f_mount(NULL, mountStr, 0);
                memset(fs, 0, sizeof(FATFS));
                MKFS_PARM opt = { XFILE_FATFS_DEFAULT_FORMAT_TYPE, 0, 0, 1, 0 };
                BYTE work[FF_MAX_SS];
                if (f_mkfs(mountStr, &opt, work, sizeof(work)) == FR_OK) {
                    fr = f_mount(fs, mountStr, 1);
                }
            }
#endif
            if (fr == FR_OK) {
                g_mountedMask |= (1u << bestIdx);
                f_chdir(mountStr);
            }
        }

        static char driveNum[8];
        if (bestIdx <= 9) {
            driveNum[0] = '0' + bestIdx;
            driveNum[1] = ':';
            driveNum[2] = '/';
            driveNum[3] = '\0';
        } else {
            snprintf(driveNum, sizeof(driveNum), "%d:/", bestIdx);
        }
        return driveNum;
    }

    /* 没有卷标前缀，使用默认驱动器 */
    *outPath = utf8Path;
    if (g_defaultIndex < 0) {
        g_defaultIndex = 0;
    }
    FATFS* fs = XFATFS_getFs(g_defaultIndex);
    if (!fs) return NULL;
    if (!(g_mountedMask & (1u << g_defaultIndex))) {
        char mountStr[4];
        mountStr[0] = '0' + g_defaultIndex;
        mountStr[1] = ':';
        mountStr[2] = '\0';

        FRESULT fr = f_mount(fs, mountStr, 1);
        if (fr == FR_NO_FILESYSTEM) {
            f_mount(NULL, mountStr, 0);
            memset(fs, 0, sizeof(FATFS));
            MKFS_PARM opt = { FM_ANY, 0, 0, 1, 0 };
            BYTE work[FF_MAX_SS];
            if (f_mkfs(mountStr, &opt, work, sizeof(work)) == FR_OK) {
                fr = f_mount(fs, mountStr, 1);
            }
        }
        if (fr == FR_OK) {
            g_mountedMask |= (1u << g_defaultIndex);
        }
    }
    static char defDrive[4];
    defDrive[0] = '0' + g_defaultIndex;
    defDrive[1] = ':';
    defDrive[2] = '/';
    defDrive[3] = '\0';
    return defDrive;
}

/**
 * @brief 将XString路径转换为Fatfs TCHAR路径
 * @param path XString路径
 * @param fatfsPath 输出Fatfs格式路径缓冲区
 * @param bufSize 缓冲区大小
 * @return 成功返回true
 */
static bool XFATFS_convertPath(const XString* path, char* fatfsPath, size_t bufSize)
{
    if (!path || !fatfsPath || bufSize == 0) return false;
    
    const char* utf8 = XString_toUtf8(path);
    if (!utf8) return false;
    
    const char* remaining;
    const char* driveStr = XFATFS_parseVolume(utf8, &remaining);
    if (!driveStr) return false;
    
    size_t driveLen = strlen(driveStr);
    size_t pathLen = strlen(remaining);
    
    /* 去掉 remaining 尾部斜杠，f_getfree 需要纯净卷标（如 "0:"） */
    while (pathLen > 0 && (remaining[pathLen - 1] == '/' || remaining[pathLen - 1] == '\\')) {
        pathLen--;
    }

    if (driveLen + pathLen >= bufSize) return false;
    
    memcpy(fatfsPath, driveStr, driveLen);
    if (pathLen > 0) {
        memcpy(fatfsPath + driveLen, remaining, pathLen);
    }
    fatfsPath[driveLen + pathLen] = '\0';
    
    return true;
}

/* ============================================================================
 * 句柄表管理 —— 通过全局 XFileDescriptor 表管理，handle 存 FIL*
 * ============================================================================ */

/** 通过 XFd 获取 FatFs FIL 指针（从 XFATFS_FileHandle 包装中取出 &fh->fil） */
static FIL* XFATFS_getFile(XFd fd)
{
    if (fd < 0) return NULL;
    XFATFS_FileHandle* fh = (XFATFS_FileHandle*)XFd_handle(fd);
    if (!fh || XFd_type(fd) != XFD_TYPE_FILE) return NULL;
    return &fh->fil;
}

/** 通过 XFd 获取完整的 XFATFS_FileHandle（含路径，供 fstat/setFileTime 使用） */
static XFATFS_FileHandle* XFATFS_getFileHandle(XFd fd)
{
    if (fd < 0) return NULL;
    XFATFS_FileHandle* fh = (XFATFS_FileHandle*)XFd_handle(fd);
    if (!fh || XFd_type(fd) != XFD_TYPE_FILE) return NULL;
    return fh;
}

/** 释放文件描述符（f_close + XFd_free + XFree_System） */
static void XFATFS_freeFile(XFd fd)
{
    XFATFS_FileHandle* fh = XFATFS_getFileHandle(fd);
    if (fh) f_close(&fh->fil);
    XFd_free(fd);
    if (fh) XFree_System(fh);
}

/** 通过 XFd 获取 FatFs 目录句柄包装 */
static XFATFS_DirHandle* XFATFS_getDirHandle(XFd fd)
{
    if (fd < 0) return NULL;
    XFATFS_DirHandle* dh = (XFATFS_DirHandle*)XFd_handle(fd);
    if (!dh || XFd_type(fd) != XFD_TYPE_DIR) return NULL;
    return dh;
}

/* ============================================================================
 * 一、核心文件操作（8个）
 * ============================================================================ */

XFd XFileSystem_open(const XString* path, int mode, int* error)
{
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) {
        if (error) *error = XFileDevice_ResourceError;
        return XFD_INVALID;
    }
    
    BYTE fatfsMode = 0;
    if (mode & XFileSystem_ReadOnly) fatfsMode |= FA_READ;
    if (mode & XFileSystem_WriteOnly) fatfsMode |= FA_WRITE;
    if ((mode & XFileSystem_ReadWrite) == XFileSystem_ReadWrite) fatfsMode |= FA_READ | FA_WRITE;
    if (mode & XFileSystem_Create)   fatfsMode |= FA_OPEN_ALWAYS;  /* 创建或打开已存在 */
    if (mode & XFileSystem_NewOnly)  fatfsMode |= FA_CREATE_NEW;    /* 仅新建，存在则失败 */
    if (mode & XFileSystem_Truncate) fatfsMode |= FA_CREATE_ALWAYS; /* 清空后打开 */
    if (mode & XFileSystem_Append) fatfsMode |= FA_OPEN_APPEND;
    if (fatfsMode == 0) fatfsMode = FA_READ;
    
    /* 分配 XFATFS_FileHandle 包装（FIL + 路径），路径供 fstat/setFileTime 使用 */
    XFATFS_FileHandle* fh = (XFATFS_FileHandle*)XMalloc_System(sizeof(XFATFS_FileHandle));
    if (!fh) {
        if (error) *error = XFileDevice_ResourceError;
        return XFD_INVALID;
    }
    memset(fh, 0, sizeof(XFATFS_FileHandle));
    strncpy(fh->path, fatfsPath, sizeof(fh->path) - 1);
    
    FRESULT fr = f_open(&fh->fil, fatfsPath, fatfsMode);
    if (fr != FR_OK) {
        XFree_System(fh);
        if (error) {
            switch (fr) {
                case FR_NO_FILE: *error = XFileDevice_OpenError; break;
                case FR_DENIED: *error = XFileDevice_PermissionsError; break;
                default: *error = XFileDevice_OpenError; break;
            }
        }
        return XFD_INVALID;
    }
    
    /* 分配全局 fd，handle 存 XFATFS_FileHandle* */
    XFd fd = XFd_alloc(XFD_TYPE_FILE, fh, NULL);
    if (fd == XFD_INVALID) {
        f_close(&fh->fil);
        XFree_System(fh);
        if (error) *error = XFileDevice_ResourceError;
        return XFD_INVALID;
    }
    
    if (mode & XFileSystem_Append) {
        f_lseek(&fh->fil, f_size(&fh->fil));
    }
    
    if (error) *error = XFileDevice_NoError;
    return fd;
}

void XFileSystem_close(XFd fd)
{
    XFATFS_freeFile(fd);
}

int64_t XFileSystem_pos(XFd fd)
{
    FIL* fp = XFATFS_getFile(fd);
    if (!fp) return -1;
    return (int64_t)f_tell(fp);
}

bool XFileSystem_seek(XFd fd, int64_t pos)
{
    FIL* fp = XFATFS_getFile(fd);
    if (!fp || pos < 0) return false;
    return f_lseek(fp, (FSIZE_t)pos) == FR_OK;
}

int64_t XFileSystem_read(XFd fd, void* buf, int64_t len)
{
    FIL* fp = XFATFS_getFile(fd);
    if (!fp || !buf || len <= 0) return -1;
    
    UINT bytesRead = 0;
    FRESULT fr = f_read(fp, buf, (UINT)len, &bytesRead);
    if (fr != FR_OK) return -1;
    return (int64_t)bytesRead;
}

int64_t XFileSystem_write(XFd fd, const void* buf, int64_t len)
{
    FIL* fp = XFATFS_getFile(fd);
    if (!fp || !buf || len <= 0) return -1;
    
    UINT bytesWritten = 0;
    FRESULT fr = f_write(fp, buf, (UINT)len, &bytesWritten);
    if (fr != FR_OK) return -1;
    return (int64_t)bytesWritten;
}

bool XFileSystem_flush(XFd fd)
{
    FIL* fp = XFATFS_getFile(fd);
    if (!fp) return false;
    return f_sync(fp) == FR_OK;
}

bool XFileSystem_resize(XFd fd, int64_t size)
{
    FIL* fp = XFATFS_getFile(fd);
    if (!fp || size < 0) return false;
    
    FSIZE_t oldPos = f_tell(fp);
    if (f_lseek(fp, (FSIZE_t)size) != FR_OK) return false;
    if (f_truncate(fp) != FR_OK) {
        f_lseek(fp, oldPos);
        return false;
    }
    f_lseek(fp, oldPos);
    return true;
}

/* ============================================================================
 * 二、文件属性操作（2个）
 * ============================================================================ */

bool XFileSystem_stat(const XString* path, XFileStat* stat)
{
    if (!path || !stat) return false;
    
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return false;
    
    FILINFO fno;
    FRESULT fr = f_stat(fatfsPath, &fno);
    if (fr != FR_OK) {
        memset(stat, 0, sizeof(XFileStat));
        stat->exists = false;
        return false;
    }
    
    memset(stat, 0, sizeof(XFileStat));
    stat->exists = true;
    stat->isFile = !(fno.fattrib & AM_DIR);
    stat->isDir = (fno.fattrib & AM_DIR) != 0;
    stat->isHidden = (fno.fattrib & AM_HID) != 0;
    stat->size = (int64_t)fno.fsize;
    stat->isReadable = true;
    stat->isWritable = !(fno.fattrib & AM_RDO);

    /* 将 FatFs 日期时间转换为 Unix 时间戳（ms） */
    {
        int year  = ((fno.fdate >> 9) & 0x7F) + 1980;
        int month = (fno.fdate >> 5) & 0x0F;
        int day   = fno.fdate & 0x1F;
        int hour  = (fno.ftime >> 11) & 0x1F;
        int min   = (fno.ftime >> 5) & 0x3F;
        int sec   = (fno.ftime & 0x1F) * 2;

        XDate date = XDate_create_date(year, month, day);
        XTime time = XTime_create_time(hour, min, sec, 0);
        XDateTime dt = XDateTime_create_datetime(date, time);
        int64_t unixSecs = XDateTime_toSecsSinceEpoch(&dt);
        stat->birthTime = unixSecs;
        stat->modificationTime = unixSecs;
        stat->accessTime = unixSecs;
        stat->metadataChangeTime = unixSecs;  /* FatFs 不区分子数据变更时间 */
    }

    return true;
}

bool XFileSystem_fstat(XFd fd, XFileStat* stat)
{
    XFATFS_FileHandle* fh = XFATFS_getFileHandle(fd);
    if (!fh || !stat) return false;

    memset(stat, 0, sizeof(XFileStat));
    stat->exists = true;
    stat->isFile = true;
    stat->isDir = false;
    stat->size = (int64_t)f_size(&fh->fil);
    stat->isReadable = true;
    stat->isWritable = true;

    /* 通过存储的路径调用 f_stat 获取真实文件时间戳 */
    FILINFO fno;
    if (f_stat(fh->path, &fno) == FR_OK) {
        int year  = ((fno.fdate >> 9) & 0x7F) + 1980;
        int month = (fno.fdate >> 5) & 0x0F;
        int day   = fno.fdate & 0x1F;
        int hour  = (fno.ftime >> 11) & 0x1F;
        int min   = (fno.ftime >> 5) & 0x3F;
        int sec   = (fno.ftime & 0x1F) * 2;

        XDate date = XDate_create_date(year, month, day);
        XTime time = XTime_create_time(hour, min, sec, 0);
        XDateTime dt = XDateTime_create_datetime(date, time);
        int64_t unixSecs = XDateTime_toSecsSinceEpoch(&dt);
        stat->birthTime = unixSecs;
        stat->modificationTime = unixSecs;
        stat->accessTime = unixSecs;
        stat->metadataChangeTime = unixSecs;
    }

    return true;
}

/* ============================================================================
 * 三、文件系统操作（4个）
 * ============================================================================ */
}

bool XFileSystem_remove(const XString* path)
{
    if (!path) return false;
    
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return false;
    
    return f_unlink(fatfsPath) == FR_OK;
}

bool XFileSystem_rename(const XString* oldPath, const XString* newPath)
{
    if (!oldPath || !newPath) return false;
    
    char oldFatfs[512], newFatfs[512];
    if (!XFATFS_convertPath(oldPath, oldFatfs, sizeof(oldFatfs))) return false;
    if (!XFATFS_convertPath(newPath, newFatfs, sizeof(newFatfs))) return false;
    
    return f_rename(oldFatfs, newFatfs) == FR_OK;
}

bool XFileSystem_copy(const XString* srcPath, const XString* dstPath)
{
    if (!srcPath || !dstPath) return false;
    
    /* 使用Fatfs读取源文件并写入目标文件 */
    char srcFatfs[512], dstFatfs[512];
    if (!XFATFS_convertPath(srcPath, srcFatfs, sizeof(srcFatfs))) return false;
    if (!XFATFS_convertPath(dstPath, dstFatfs, sizeof(dstFatfs))) return false;
    
    FIL src, dst;
    if (f_open(&src, srcFatfs, FA_READ) != FR_OK) return false;
    if (f_open(&dst, dstFatfs, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        f_close(&src);
        return false;
    }
    
    uint8_t buf[512];
    UINT bytesRead, bytesWritten;
    bool success = true;
    
    while (1) {
        if (f_read(&src, buf, sizeof(buf), &bytesRead) != FR_OK) {
            success = false;
            break;
        }
        if (bytesRead == 0) break;
        
        if (f_write(&dst, buf, bytesRead, &bytesWritten) != FR_OK || bytesWritten != bytesRead) {
            success = false;
            break;
        }
    }
    
    f_close(&src);
    f_close(&dst);
    return success;
}

/* ============================================================================
 * 四、目录操作（5个）
 * ============================================================================ */

bool XFileSystem_mkdir(const XString* path, bool recursive)
{
    if (!path) return false;
    
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return false;
    
    if (recursive) {
        /* 递归创建父目录 */
        char temp[512];
        strcpy(temp, fatfsPath);
        for (char* p = temp; *p; p++) {
            if (*p == '/' && p != temp && *(p - 1) != ':') {
                *p = '\0';
                f_mkdir(temp);
                *p = '/';
            }
        }
    }
    
    return f_mkdir(fatfsPath) == FR_OK;
}

bool XFileSystem_rmdir(const XString* path, bool recursive)
{
    if (!path) return false;
    
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return false;
    
    if (!recursive) {
        return f_unlink(fatfsPath) == FR_OK;
    }
    
    /* 递归删除目录内容 */
    DIR dir;
    if (f_opendir(&dir, fatfsPath) != FR_OK) {
        return f_unlink(fatfsPath) == FR_OK;
    }
    
    FILINFO fno;
    char fullPath[512];
    size_t baseLen = strlen(fatfsPath);
    
    strcpy(fullPath, fatfsPath);
    if (baseLen > 0 && fullPath[baseLen - 1] != '/') {
        fullPath[baseLen++] = '/';
        fullPath[baseLen] = '\0';
    }
    
    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
        if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) continue;
        
        strcpy(fullPath + baseLen, fno.fname);
        
        if (fno.fattrib & AM_DIR) {
            XString* subPath = XString_create_utf8(fullPath);
            XFileSystem_rmdir(subPath, true);
            XString_delete_base(subPath);
        } else {
            f_unlink(fullPath);
        }
    }
    
    f_closedir(&dir);
    return f_unlink(fatfsPath) == FR_OK;
}

XDirIterator XFileSystem_opendir(const XString* path)
{
    if (!path) return NULL;
    
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return NULL;
    
    /* 分配 XFATFS_DirHandle 包装，通过 XFileDescriptor 表管理（XFD_TYPE_DIR） */
    XFATFS_DirHandle* dh = (XFATFS_DirHandle*)XMalloc_System(sizeof(XFATFS_DirHandle));
    if (!dh) return NULL;
    memset(dh, 0, sizeof(XFATFS_DirHandle));
    
    if (f_opendir(&dh->dir, fatfsPath) != FR_OK) {
        XFree_System(dh);
        return NULL;
    }
    
    XFd fd = XFd_alloc(XFD_TYPE_DIR, dh, NULL);
    if (fd == XFD_INVALID) {
        f_closedir(&dh->dir);
        XFree_System(dh);
        return NULL;
    }
    
    return (XDirIterator)(uintptr_t)fd;
}

bool XFileSystem_readdir(XDirIterator iter, XDirEntry* entry)
{
    if (!iter || !entry) return false;
    
    XFd fd = (XFd)(intptr_t)(uintptr_t)iter;
    XFATFS_DirHandle* dh = XFATFS_getDirHandle(fd);
    if (!dh) return false;
    
    FRESULT fr = f_readdir(&dh->dir, &dh->info);
    if (fr != FR_OK || dh->info.fname[0] == 0) return false;
    
    /* 设置文件名 */
    if (entry->name) {
        XString_assign_utf8(entry->name, dh->info.fname);
    }
    
    entry->isDir = (dh->info.fattrib & AM_DIR) != 0;
    entry->isFile = !entry->isDir;
    entry->isHidden = (dh->info.fattrib & AM_HID) != 0;
    entry->isSymLink = false;
    
    return true;
}

void XFileSystem_closedir(XDirIterator iter)
{
    if (!iter) return;
    
    XFd fd = (XFd)(intptr_t)(uintptr_t)iter;
    XFATFS_DirHandle* dh = XFATFS_getDirHandle(fd);
    if (dh) {
        f_closedir(&dh->dir);
    }
    XFd_free(fd);
    if (dh) XFree_System(dh);
}

/* ============================================================================
 * 五、路径操作（1个）
 * ============================================================================ */

bool XFileSystem_resolvePath(const XString* path, XString* result, XPathStyle style)
{
    if (!path || !result) return false;

    /* Fatfs 模式下直接返回绝对路径：通过 f_getcwd 获取当前工作目录并拼接 */
    char cwd[256];
    if (f_getcwd(cwd, sizeof(cwd)) != FR_OK) return false;

    /* 转换Fatfs格式 "0:/" → "C:/" */
    char absPath[256];
    if (cwd[0] >= '0' && cwd[0] <= '9' && cwd[1] == ':') {
        int volIdx = cwd[0] - '0';
        if (volIdx >= 0 && volIdx < XFATFS_MAX_VOLUMES && g_volumes[volIdx]) {
            XString* xDrive = XString_create();
            if (xDrive && XFatfsDrives_at(volIdx, xDrive)) {
                const char* driveUtf8 = XString_toUtf8(xDrive);
                if (driveUtf8) {
                    snprintf(absPath, sizeof(absPath), "%s%s", driveUtf8, cwd + 2);
                }
            }
            if (xDrive) XString_delete_base(xDrive);
            if (absPath[0] == '\0') strcpy(absPath, cwd);
        } else {
            strcpy(absPath, cwd);
        }
    } else {
        strcpy(absPath, cwd);
    }

    XString_assign_utf8(result, absPath);
    (void)style;
    return true;
}
bool XFileSystem_getSpecialPath(XSpecialPath type, XString* path)
{
    if (!path) return false;
    switch (type) {
    case XSpecialPath_Current: {
        char cwd[256];
        FRESULT fr = f_getcwd(cwd, sizeof(cwd));
        if (fr != FR_OK) return XFatfsPath_current(path);
        
        if (cwd[0] >= '0' && cwd[0] <= '9' && cwd[1] == ':') {
            int volIdx = cwd[0] - '0';
            if (volIdx >= 0 && volIdx < XFATFS_MAX_VOLUMES && g_volumes[volIdx]) {
                XString* xDrive = XString_create();
                if (xDrive && XFatfsDrives_at(volIdx, xDrive)) {
                    const char* driveUtf8 = XString_toUtf8(xDrive);
                    if (driveUtf8) {
                        char converted[256];
                        snprintf(converted, sizeof(converted), "%s%s", driveUtf8, cwd + 2);
                        XString_assign_utf8(path, converted);
                        XString_delete_base(xDrive);
                        return true;
                    }
                }
                if (xDrive) XString_delete_base(xDrive);
            }
        }
        XString_assign_utf8(path, cwd);
        return true;
    }
    case XSpecialPath_Home:
        return XFatfsPath_home(path);
    case XSpecialPath_Root:
        return XFatfsPath_root(path);
    case XSpecialPath_Temp:
        return XFatfsPath_temp(path);
    default:
        return false;
    }
}

bool XFileSystem_setCurrentPath(const XString* path)
{
    if (!path) return false;
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return false;
    FRESULT fr = f_chdir(fatfsPath);
    if (fr == FR_OK) return true;
    return XFatfsPath_setCurrent(path);
}

/* ============================================================================
 * 七、符号链接操作（2个）- 不支持
 * ============================================================================ */

bool XFileSystem_link(const XString* targetPath, const XString* linkPath)
{
    (void)targetPath;
    (void)linkPath;
    return false;
}

bool XFileSystem_readLink(const XString* path, XString* target)
{
    (void)path;
    (void)target;
    return false;
}

/* ============================================================================
 * 八、权限操作（1个）- 支持基本只读属性
 * ============================================================================ */

bool XFileSystem_setPermissions(const XString* path, XFilePermissions permissions)
{
    if (!path) return false;
    
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return false;
    
    BYTE attr = 0;
    BYTE mask = AM_RDO | AM_HID | AM_SYS | AM_ARC;
    
    if (!(permissions & 0x0200)) attr |= AM_RDO;
    
    return f_chmod(fatfsPath, attr, mask) == FR_OK;
}

/* ============================================================================
 * 九、内存映射（2个）- 不支持
 * ============================================================================ */

void* XFileSystem_map(XFd fd, int64_t offset, int64_t size, bool writable)
{
    FIL* fp = XFATFS_getFile(fd);
    if (!fp || size <= 0) return NULL;
    
    void* buf = XMalloc_System((size_t)size);
    if (!buf) return NULL;
    
    FSIZE_t oldPos = f_tell(fp);
    f_lseek(fp, (FSIZE_t)offset);
    
    UINT bytesRead;
    if (f_read(fp, buf, (UINT)size, &bytesRead) != FR_OK || bytesRead != (UINT)size) {
        XFree_System(buf);
        f_lseek(fp, oldPos);
        return NULL;
    }
    
    f_lseek(fp, oldPos);
    return buf;
}

bool XFileSystem_unmap(void* addr, int64_t size)
{
    if (!addr) return false;
    XFree_System(addr);
    return true;
}

/**
 * @brief 通过文件描述符设置文件时间
 * @param fd 文件描述符（XFileDescriptor 表索引）
 * @param timeType 时间类型（访问时间/修改时间/创建时间）
 * @param timeValue 时间值（Unix时间戳，秒）
 * @return 成功返回true
 * @note FatFs 无 f_futime 接口，通过 XFATFS_FileHandle 中存储的路径
 *       调用 f_utime 实现。路径版需求由上层通过 open→setFileTime→close 组合实现。
 */
bool XFileSystem_setFileTime(XFd fd, XFileTime timeType, int64_t timeValue)
{
    XFATFS_FileHandle* fh = XFATFS_getFileHandle(fd);
    if (!fh) return false;

    /* 将 Unix 时间戳转换为 FatFs 日期时间格式 */
    XDateTime dt;
    XDateTime_setSecsSinceEpoch(&dt, timeValue);
    XDate date = XDateTime_date(&dt);
    XTime time = XDateTime_time(&dt);

    FILINFO fno;
    memset(&fno, 0, sizeof(fno));
    fno.fdate = (WORD)(((XDate_year(&date) - 1980) << 9) | (XDate_month(&date) << 5) | XDate_day(&date));
    fno.ftime = (WORD)((XTime_hour(&time) << 11) | (XTime_minute(&time) << 5) | (XTime_second(&time) / 2));

    (void)timeType; /* FatFs 的 f_utime 同时设置修改时间和创建时间 */

    return f_utime(fh->path, &fno) == FR_OK;
}

/* ============================================================================
 * 十二、驱动器列表
 * ============================================================================ */

int XFileSystem_drives_count(void)
{
    return XFatfsDrives_count();
}

bool XFileSystem_drives_at(int index, XString* path)
{
    return XFatfsDrives_at(index, path);
}

/* ============================================================================
 * 十三、存储设备信息
 * ============================================================================ */

bool XFileSystem_getStorageInfo(const XString* path, XStorageInfoData* info)
{
    if (!path || !info) return false;
    
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return false;
    
    DWORD freeClusters;
    FATFS* fs;
    
    FRESULT fr = f_getfree(fatfsPath, &freeClusters, &fs);
    if (fr != FR_OK) return false;
    
    uint32_t totalSectors = (uint32_t)((fs->n_fatent - 2) * fs->csize);
    uint32_t freeSectors = freeClusters * fs->csize;
    
    info->bytesTotal = (int64_t)totalSectors * FF_MAX_SS;
    info->bytesFree = (int64_t)freeSectors * FF_MAX_SS;
    info->bytesAvailable = info->bytesFree;
    info->blockSize = fs->csize * FF_MAX_SS;
    info->isReadOnly = (fs->fs_type == 0);
    info->isValid = true;
    info->isReady = true;
    
    /* 设置文件系统类型 */
    if (info->fileSystemType) {
        switch (fs->fs_type) {
            case FS_FAT12: XString_assign_utf8(info->fileSystemType, "FAT12"); break;
            case FS_FAT16: XString_assign_utf8(info->fileSystemType, "FAT16"); break;
            case FS_FAT32: XString_assign_utf8(info->fileSystemType, "FAT32"); break;
            case FS_EXFAT: XString_assign_utf8(info->fileSystemType, "exFAT"); break;
            default: XString_assign_utf8(info->fileSystemType, ""); break;
        }
    }
    
    return true;
}

/* ============================================================================
 * 十四、磁盘格式化
 * ============================================================================ */

bool XFileSystem_format(const XString* drive,
                        XFileSystemType fsType,
                        const XString* volumeName,
                        int flags,
                        int clusterSize,
                        XFileSystemFormatProgress progress,
                        void* userData)
{
    if (!drive) return false;
    
    char fatfsPath[512];
    if (!XFATFS_convertPath(drive, fatfsPath, sizeof(fatfsPath))) return false;
    
    MKFS_PARM opt = {0};
    opt.fmt = FM_ANY;
    
    if (fsType == XFileSystemType_FAT32) opt.fmt = FM_FAT32;
    else if (fsType == XFileSystemType_exFAT) opt.fmt = FM_EXFAT;
    
    if (clusterSize > 0) opt.au_size = (DWORD)clusterSize;
    
    /* 调用进度回调 */
    if (progress) progress(50, userData);
    
    BYTE work[FF_MAX_SS];
    FRESULT fr = f_mkfs(fatfsPath, &opt, work, sizeof(work));
    
    if (progress) progress(100, userData);
    
    (void)volumeName;
    (void)flags;
    
    return fr == FR_OK;
}

#endif /* XFILE_USE_FATFS */
