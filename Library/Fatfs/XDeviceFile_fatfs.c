#include "XFileSystem_config.h"

/* 仅在启用FatFS模式时编译此文件 */
#if defined(XFILE_USE_FATFS)

#include "XDeviceFile.h"
#include "XFileDevice.h"
#include "XMemory.h"
#include "XString.h"
#include "XDateTime.h"        /* XDateTime_currentDateTime 等 */
#include "XTypes.h"           /* XFd, XFD_INVALID */
#include "XFileDescriptor.h"  /* XFd_alloc, XFd_free, XFd_handle */
#include "XAtomic.h"           /* 卷状态一次性初始化同步 */
#include "XMutex.h"            /* 保护卷表、挂载位图和默认卷 */
#include "ff.h"               /* FatFs API */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 这些函数只服务于 XDeviceFile/XDevice 对旧 XFd 的兼容路径。 */
void XDeviceFile_legacyClose(XFd fd);
int64_t XDeviceFile_legacyRead(XFd fd, void* buffer, int64_t size);
int64_t XDeviceFile_legacyWrite(XFd fd, const void* data, int64_t size);
int64_t XDeviceFile_legacySeek(XFd fd, int64_t offset, XSeekWhence whence);
bool XDeviceFile_legacyFlush(XFd fd);
bool XDeviceFile_legacyResize(XFd fd, int64_t size);

/* diskio 平台文件只为本 FatFs 实现提供的内部适配符号，不进入 XDeviceFile 公共头。 */
int XFATFS_platformDriveCount(void);
bool XFATFS_platformDriveAt(int index, XString* path);
int XFATFS_platformDrivePrefixToIndex(const char* prefix);
bool XFATFS_platformCurrentPath(XString* path);
bool XFATFS_platformSetCurrentPath(const XString* path);
bool XFATFS_platformHomePath(XString* path);
bool XFATFS_platformRootPath(XString* path);
bool XFATFS_platformTempPath(XString* path);

/* ============================================================================
 * RTC 函数 - get_fattime（Fatfs 要求的实时时钟，跨平台通用）
 * ============================================================================ */

DWORD get_fattime(void)
{
    XDateTime now = XDateTime_currentDateTime();
    XDate date = XDateTime_date(&now);
    XTime time = XDateTime_time(&now);
    int year = XDate_year(&date);
    if (year < 1980) year = 1980;
    if (year > 2107) year = 2107;
    
    return (DWORD)((year - 1980) << 25)
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
static XMutex* g_volumeStateMutex = NULL;       /* 卷状态互斥锁 */
static XAtomic_int32_t g_volumeStateInit = {0}; /* 0=未初始化，1=初始化中，2=已就绪，-1=失败 */

/**
 * @brief 构造 FatFs 的十进制卷字符串。
 * @param index 卷索引。
 * @param buffer 输出缓冲区。
 * @param size 输出缓冲区大小。
 * @return 成功返回true，索引越界或缓冲区不足返回false。
 */
static bool XFATFS_makeMountString(int index, char* buffer, size_t size)
{
    int length;
    if (!buffer || size == 0 || index < 0 || index >= XFATFS_MAX_VOLUMES) return false;
    length = snprintf(buffer, size, "%d:", index);
    return length >= 0 && (size_t)length < size;
}

/**
 * @brief 获取卷状态互斥锁。
 * @return 初始化成功返回锁指针，内存不足时返回NULL。
 * @note 采用原子状态完成一次性初始化，避免多个线程同时创建锁。
 */
static XMutex* XFATFS_stateMutex(void)
{
    int32_t expected = 0;
    if (XAtomic_compare_exchange_strong_int32(&g_volumeStateInit, &expected, 1,
            XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
        XMutex* mutex = XMutex_create(XLock_NonRecursive);
        g_volumeStateMutex = mutex;
        XAtomic_store_int32(&g_volumeStateInit, mutex ? 2 : -1,
                            XAtomic_MemoryOrder_Release);
    } else {
        while (XAtomic_load_int32(&g_volumeStateInit, XAtomic_MemoryOrder_Acquire) == 1) {
            /* 等待创建线程发布互斥锁指针。 */
        }
    }
    return XAtomic_load_int32(&g_volumeStateInit, XAtomic_MemoryOrder_Acquire) == 2
        ? g_volumeStateMutex : NULL;
}

/**
 * @brief 锁定卷状态。
 * @return 成功获得锁返回true，锁创建失败返回false。
 */
static bool XFATFS_lockState(void)
{
    XMutex* mutex = XFATFS_stateMutex();
    if (!mutex) return false;
    XMutex_lock(mutex);
    return true;
}

/**
 * @brief 解锁卷状态。
 */
static void XFATFS_unlockState(void)
{
    if (g_volumeStateMutex) XMutex_unlock(g_volumeStateMutex);
}

/**
 * @brief 判断卷对象是否已经分配。
 * @param idx FatFs卷索引。
 * @return 已分配返回true，否则返回false。
 */
static bool XFATFS_isVolumeAllocated(int idx)
{
    bool allocated = false;
    if (!XFATFS_lockState()) return false;
    if (idx >= 0 && idx < XFATFS_MAX_VOLUMES) allocated = g_volumes[idx] != NULL;
    XFATFS_unlockState();
    return allocated;
}

/**
 * @brief 在卷状态锁内读取FatFs当前目录。
 * @param buffer 当前目录输出缓冲区。
 * @param size 输出缓冲区大小。
 * @return FatFs操作结果。
 */
static FRESULT XFATFS_getCurrentDirectory(char* buffer, size_t size)
{
    FRESULT result;
    if (!XFATFS_lockState()) return FR_NOT_READY;
    result = f_getcwd(buffer, size);
    XFATFS_unlockState();
    return result;
}

/**
 * @brief 在卷状态锁内切换FatFs当前目录。
 * @param path FatFs格式路径。
 * @return FatFs操作结果。
 */
static FRESULT XFATFS_setCurrentDirectory(const char* path)
{
    FRESULT result;
    if (!XFATFS_lockState()) return FR_NOT_READY;
    result = f_chdir(path);
    XFATFS_unlockState();
    return result;
}

/**
 * @brief 获取或分配指定索引的 FATFS*。
 * @note 调用方必须已经持有 XFATFS_lockState() 返回的卷状态锁。
 */
static FATFS* XFATFS_getFsLocked(int idx)
{
    if (idx < 0 || idx >= XFATFS_MAX_VOLUMES) return NULL;
    if (!g_volumes[idx]) {
        g_volumes[idx] = (FATFS*)XMalloc_System(sizeof(FATFS));
        if (g_volumes[idx]) memset(g_volumes[idx], 0, sizeof(FATFS));
    }
    return g_volumes[idx];
}

/**
 * @brief 在已持有卷状态锁时解析卷标并完成懒挂载。
 * @param utf8Path UTF-8路径（如 "C:/file.txt"）。
 * @param outPath 输出去掉卷标前缀的路径。
 * @param driveBuffer 调用方提供的FatFs驱动器字符串缓冲区。
 * @param driveBufferSize 驱动器字符串缓冲区大小。
 * @return FatFs驱动器字符串（如 "0:/"），失败返回NULL。
 * @note 本函数只能由 XFATFS_parseVolume() 在持锁状态下调用。
 */
static const char* XFATFS_parseVolumeLocked(const char* utf8Path,
                                            const char** outPath,
                                            char* driveBuffer,
                                            size_t driveBufferSize)
{
    if (!utf8Path || !outPath || !driveBuffer || driveBufferSize < 4 ||
        XFATFS_platformDriveCount() <= 0) return NULL;
    /* 通过平台函数在驱动器前缀列表中查找索引 */
    int bestIdx = XFATFS_platformDrivePrefixToIndex(utf8Path);
    if (bestIdx >= 0) {
        if (bestIdx >= FF_VOLUMES) return NULL;
        /* 获取匹配的前缀长度（如 "C:"=2, "sd:"=3） */
        XString* xDrive = XString_create();
        if (!xDrive) return NULL;
        char driveName[64];
        const char* driveUtf8 = NULL;
        size_t bestLen = 0;
        if (XFATFS_platformDriveAt(bestIdx, xDrive)) {
            const char* driveText = XString_toUtf8(xDrive);
            if (driveText) {
                bestLen = strlen(driveText);
                if (bestLen >= sizeof(driveName)) bestLen = sizeof(driveName) - 1;
                memcpy(driveName, driveText, bestLen);
                driveName[bestLen] = '\0';
                driveUtf8 = driveName;
            }
        }
        XString_delete_base(xDrive);
        if (!driveUtf8 || bestLen == 0) return NULL;

        *outPath = utf8Path + bestLen;
        while (**outPath == '/' || **outPath == '\\') (*outPath)++;

        FATFS* fs = XFATFS_getFsLocked(bestIdx);
        if (!fs) return NULL;

        if (g_defaultIndex < 0) g_defaultIndex = bestIdx;

        if (!(g_mountedMask & (1u << bestIdx))) {
            char mountStr[8];
            if (!XFATFS_makeMountString(bestIdx, mountStr, sizeof(mountStr))) return NULL;

            FRESULT fr = f_mount(fs, mountStr, 1);
#if XFILE_FATFS_DISKIO_MODE == 0 && !FF_FS_READONLY && FF_USE_MKFS
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

        if (driveBufferSize < 4) return NULL;
        if (bestIdx <= 9) {
            driveBuffer[0] = '0' + bestIdx;
            driveBuffer[1] = ':';
            driveBuffer[2] = '/';
            driveBuffer[3] = '\0';
        } else {
            int n = snprintf(driveBuffer, driveBufferSize, "%d:/", bestIdx);
            if (n < 0 || (size_t)n >= driveBufferSize) return NULL;
        }
        return driveBuffer;
    }

    /* 没有卷标前缀，使用默认驱动器 */
    *outPath = utf8Path;
    if (g_defaultIndex < 0) {
        g_defaultIndex = 0;
    }
    if (g_defaultIndex >= FF_VOLUMES) return NULL;
    FATFS* fs = XFATFS_getFsLocked(g_defaultIndex);
    if (!fs) return NULL;
    if (!(g_mountedMask & (1u << g_defaultIndex))) {
        char mountStr[8];
        if (!XFATFS_makeMountString(g_defaultIndex, mountStr, sizeof(mountStr))) return NULL;

        FRESULT fr = f_mount(fs, mountStr, 1);
        if (fr == FR_NO_FILESYSTEM) {
#if !FF_FS_READONLY && FF_USE_MKFS
            f_mount(NULL, mountStr, 0);
            memset(fs, 0, sizeof(FATFS));
            MKFS_PARM opt = { FM_ANY, 0, 0, 1, 0 };
            BYTE work[FF_MAX_SS];
            if (f_mkfs(mountStr, &opt, work, sizeof(work)) == FR_OK) {
                fr = f_mount(fs, mountStr, 1);
            }
#endif
        }
        if (fr == FR_OK) {
            g_mountedMask |= (1u << g_defaultIndex);
        }
    }
    if (driveBufferSize < 4) return NULL;
    if (snprintf(driveBuffer, driveBufferSize, "%d:/", g_defaultIndex) < 0 ||
        strlen(driveBuffer) + 1 > driveBufferSize) return NULL;
    return driveBuffer;
}

/**
 * @brief 解析卷标并在必要时完成懒挂载。
 * @param utf8Path UTF-8路径（如 "C:/file.txt"）。
 * @param outPath 输出去掉卷标前缀的路径。
 * @param driveBuffer 调用方提供的FatFs驱动器字符串缓冲区。
 * @param driveBufferSize 驱动器字符串缓冲区大小。
 * @return FatFs驱动器字符串（如 "0:/"），失败返回NULL。
 */
static const char* XFATFS_parseVolume(const char* utf8Path,
                                      const char** outPath,
                                      char* driveBuffer,
                                      size_t driveBufferSize)
{
    if (!XFATFS_lockState()) return NULL;
    const char* result = XFATFS_parseVolumeLocked(utf8Path, outPath,
                                                  driveBuffer, driveBufferSize);
    XFATFS_unlockState();
    return result;
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
    char driveBuffer[8];
    const char* driveStr = XFATFS_parseVolume(utf8, &remaining,
                                              driveBuffer, sizeof(driveBuffer));
    if (!driveStr) return false;
    
    size_t driveLen = strlen(driveStr);
    size_t pathLen = strlen(remaining);
    size_t pathStart = 0;

    /* FatFs 对卷标后的重复分隔符处理并不一致，统一压成一个根分隔符。 */
    while (pathStart < pathLen &&
           (remaining[pathStart] == '/' || remaining[pathStart] == '\\')) {
        ++pathStart;
    }
    remaining += pathStart;
    pathLen -= pathStart;
    
    /* 去掉 remaining 尾部斜杠，f_getfree 需要纯净卷标（如 "0:"） */
    while (pathLen > 0 && (remaining[pathLen - 1] == '/' || remaining[pathLen - 1] == '\\')) {
        pathLen--;
    }

    if (driveLen + pathLen >= bufSize) return false;
    
    memcpy(fatfsPath, driveStr, driveLen);
    if (pathLen > 0) {
        /* driveStr 默认以 '/' 结尾；相对路径直接拼接即可。 */
        if (driveLen > 0 && (fatfsPath[driveLen - 1] == '/' ||
                             fatfsPath[driveLen - 1] == '\\')) {
            memcpy(fatfsPath + driveLen, remaining, pathLen);
        } else {
            fatfsPath[driveLen] = '/';
            memcpy(fatfsPath + driveLen + 1, remaining, pathLen);
            ++driveLen;
        }
    }
    fatfsPath[driveLen + pathLen] = '\0';
    for (size_t i = 0; i < driveLen + pathLen; ++i) {
        if (fatfsPath[i] == '\\') fatfsPath[i] = '/';
    }
    
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

XFd XDeviceFile_legacyOpen(const XString* path, int mode, uint32_t openFlags, int* error)
{
    if (openFlags != XDeviceOpenFlag_None) {
        if (error) *error = XFileDevice_OpenError;
        return XFD_INVALID;
    }
#if FF_FS_READONLY
    if (mode & (XDeviceFile_WriteOnly | XDeviceFile_ReadWrite | XDeviceFile_Append |
                XDeviceFile_Truncate | XDeviceFile_Create | XDeviceFile_NewOnly)) {
        if (error) *error = XFileDevice_PermissionsError;
        return XFD_INVALID;
    }
#endif
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) {
        if (error) *error = XFileDevice_ResourceError;
        return XFD_INVALID;
    }
    
    BYTE fatfsMode = 0;
    if (mode & XDeviceFile_ReadOnly) fatfsMode |= FA_READ;
    if (mode & XDeviceFile_WriteOnly) fatfsMode |= FA_WRITE;
    if ((mode & XDeviceFile_ReadWrite) == XDeviceFile_ReadWrite) fatfsMode |= FA_READ | FA_WRITE;
    if (mode & XDeviceFile_Create)   fatfsMode |= FA_OPEN_ALWAYS;  /* 创建或打开已存在 */
    if (mode & XDeviceFile_NewOnly)  fatfsMode |= FA_CREATE_NEW;    /* 仅新建，存在则失败 */
    if (mode & XDeviceFile_Truncate) fatfsMode |= FA_CREATE_ALWAYS; /* 清空后打开 */
    if (mode & XDeviceFile_Append) fatfsMode |= FA_OPEN_APPEND;
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
    
    if (mode & XDeviceFile_Append) {
        f_lseek(&fh->fil, f_size(&fh->fil));
    }
    
    if (error) *error = XFileDevice_NoError;
    return fd;
}

XFd XDeviceFile_openStandardInput(int* error)
{
    if (error) *error = XFileDevice_OpenError;
    return XFD_INVALID;
}

void XDeviceFile_legacyClose(XFd fd)
{
    XFATFS_freeFile(fd);
}

int64_t XDeviceFile_legacySeek(XFd fd, int64_t offset, XSeekWhence whence)
{
    FIL* fp = XFATFS_getFile(fd);
    if (!fp) return -1;
    FSIZE_t target;
    switch (whence) {
        case XSeekSet: target = (FSIZE_t)offset; break;
        case XSeekCur: target = (FSIZE_t)((int64_t)f_tell(fp) + offset); break;
        case XSeekEnd: target = (FSIZE_t)((int64_t)f_size(fp) + offset); break;
        default: return -1;
    }
    if ((int64_t)target < 0 || f_lseek(fp, target) != FR_OK) return -1;
    return (int64_t)f_tell(fp);
}

int64_t XDeviceFile_legacyRead(XFd fd, void* buf, int64_t len)
{
    FIL* fp = XFATFS_getFile(fd);
    if (!fp || len < 0 || (len > 0 && !buf) || (uint64_t)len > UINT_MAX) return -1;
    if (len == 0) return 0;
    
    UINT bytesRead = 0;
    FRESULT fr = f_read(fp, buf, (UINT)len, &bytesRead);
    if (fr != FR_OK) return -1;
    return (int64_t)bytesRead;
}

bool XDeviceFile_legacySetStandardInputEcho(XFd fd, bool enabled)
{
    /* FatFS 只提供块设备文件访问，不拥有终端控制台及回显状态。 */
    (void)fd;
    (void)enabled;
    return false;
}

int64_t XDeviceFile_legacyWrite(XFd fd, const void* buf, int64_t len)
{
#if FF_FS_READONLY
    (void)fd;
    (void)buf;
    (void)len;
    return -1;
#else
    FIL* fp = XFATFS_getFile(fd);
    if (!fp || len < 0 || (len > 0 && !buf) || (uint64_t)len > UINT_MAX) return -1;
    if (len == 0) return 0;
    
    UINT bytesWritten = 0;
    FRESULT fr = f_write(fp, buf, (UINT)len, &bytesWritten);
    if (fr != FR_OK) return -1;
    return (int64_t)bytesWritten;
#endif
}

bool XDeviceFile_legacyFlush(XFd fd)
{
    FIL* fp = XFATFS_getFile(fd);
    if (!fp) return false;
#if FF_FS_READONLY
    return true;
#else
    return f_sync(fp) == FR_OK;
#endif
}

bool XDeviceFile_legacyResize(XFd fd, int64_t size)
{
#if FF_FS_READONLY
    (void)fd;
    (void)size;
    return false;
#else
    FIL* fp = XFATFS_getFile(fd);
    if (!fp || size < 0) return false;
    
    FSIZE_t oldPos = f_tell(fp);
    FSIZE_t restorePos;
    if (f_lseek(fp, (FSIZE_t)size) != FR_OK) return false;
    if (f_truncate(fp) != FR_OK) {
        f_lseek(fp, oldPos);
        return false;
    }
    /* FatFs 在写模式下定位到 EOF 之后会扩展文件；截断后不能恢复一个
       超过新大小的旧位置，否则会撤销刚完成的截断。 */
    restorePos = oldPos > (FSIZE_t)size ? (FSIZE_t)size : oldPos;
    f_lseek(fp, restorePos);
    return true;
#endif
}

/* ============================================================================
 * 二、文件属性操作（2个）
 * ============================================================================ */

bool XDeviceFile_stat(const XString* path, XFileStat* stat)
{
    if (!path || !stat) return false;
    
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return false;
    
    FILINFO fno;
    FRESULT fr = f_stat(fatfsPath, &fno);
    if (fr != FR_OK) {
        /* FatFs 某些版本对卷根目录的 f_stat 返回 FR_INVALID_NAME，
         * 通过打开目录确认根路径存在，保持 XDeviceFile_stat 的目录语义。 */
        DIR rootDir;
        if (f_opendir(&rootDir, fatfsPath) == FR_OK) {
            f_closedir(&rootDir);
            memset(stat, 0, sizeof(XFileStat));
            stat->exists = true;
            stat->isDir = true;
            stat->isReadable = true;
            stat->isWritable = true;
            stat->permissions = XFile_ReadOwner | XFile_WriteOwner |
                                XFile_ReadUser | XFile_WriteUser |
                                XFile_ReadGroup | XFile_WriteGroup;
            return true;
        }
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
    stat->permissions = XFile_ReadOwner | XFile_ReadUser | XFile_ReadGroup |
                        (stat->isWritable ? (XFile_WriteOwner | XFile_WriteUser | XFile_WriteGroup) : 0);

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

bool XDeviceFile_legacyFstat(XFd fd, XFileStat* stat)
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
    stat->permissions = XFile_ReadOwner | XFile_ReadUser | XFile_ReadGroup |
                        XFile_WriteOwner | XFile_WriteUser | XFile_WriteGroup;

    /* 通过存储的路径调用 f_stat 获取真实文件时间戳 */
    FILINFO fno;
    if (f_stat(fh->path, &fno) == FR_OK) {
        stat->isWritable = !(fno.fattrib & AM_RDO);
        stat->permissions = XFile_ReadOwner | XFile_ReadUser | XFile_ReadGroup |
                            (stat->isWritable ? (XFile_WriteOwner | XFile_WriteUser | XFile_WriteGroup) : 0);
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
bool XDeviceFile_remove(const XString* path, XRemoveMode mode, XString* trashPath)
{
    (void)mode;
    (void)trashPath;
#if FF_FS_READONLY
    (void)path;
    return false;
#else
    if (!path) return false;
    
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return false;
    
    return f_unlink(fatfsPath) == FR_OK;
#endif
}

bool XDeviceFile_rename(const XString* oldPath, const XString* newPath)
{
#if FF_FS_READONLY
    (void)oldPath;
    (void)newPath;
    return false;
#else
    if (!oldPath || !newPath) return false;
    
    char oldFatfs[512], newFatfs[512];
    if (!XFATFS_convertPath(oldPath, oldFatfs, sizeof(oldFatfs))) return false;
    if (!XFATFS_convertPath(newPath, newFatfs, sizeof(newFatfs))) return false;
    
    return f_rename(oldFatfs, newFatfs) == FR_OK;
#endif
}

bool XDeviceFile_copy(const XString* srcPath, const XString* dstPath)
{
#if FF_FS_READONLY
    (void)srcPath;
    (void)dstPath;
    return false;
#else
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
#endif
}

/* ============================================================================
 * 四、目录操作（5个）
 * ============================================================================ */

bool XDeviceFile_mkdir(const XString* path, bool recursive)
{
#if FF_FS_READONLY
    (void)path;
    (void)recursive;
    return false;
#else
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
#endif
}

bool XDeviceFile_rmdir(const XString* path, bool recursive)
{
#if FF_FS_READONLY
    (void)path;
    (void)recursive;
    return false;
#else
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
    if (baseLen >= sizeof(fullPath) - 1) {
        f_closedir(&dir);
        return false;
    }
    
    strcpy(fullPath, fatfsPath);
    if (baseLen > 0 && fullPath[baseLen - 1] != '/') {
        fullPath[baseLen++] = '/';
        fullPath[baseLen] = '\0';
    }
    
    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
        if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) continue;
        
        size_t nameLen = strlen(fno.fname);
        if (baseLen + nameLen >= sizeof(fullPath)) {
            f_closedir(&dir);
            return false;
        }
        memcpy(fullPath + baseLen, fno.fname, nameLen + 1);
        
        if (fno.fattrib & AM_DIR) {
            XString* subPath = XString_create_utf8(fullPath);
            XDeviceFile_rmdir(subPath, true);
            XString_delete_base(subPath);
        } else {
            f_unlink(fullPath);
        }
    }
    
    f_closedir(&dir);
    return f_unlink(fatfsPath) == FR_OK;
#endif
}

XDirIterator XDeviceFile_opendir(const XString* path)
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
    
    /* XDirIterator 使用 NULL 表示失败，不能直接把合法的 fd=0 转成指针。 */
    return (XDirIterator)(uintptr_t)(fd + 1);
}

bool XDeviceFile_readdir(XDirIterator iter, XDirEntry* entry)
{
    if (!iter || !entry) return false;
    
    uintptr_t encoded = (uintptr_t)iter;
    if (encoded == 0) return false;
    XFd fd = (XFd)(encoded - 1);
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

void XDeviceFile_closedir(XDirIterator iter)
{
    if (!iter) return;
    
    uintptr_t encoded = (uintptr_t)iter;
    if (encoded == 0) return;
    XFd fd = (XFd)(encoded - 1);
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

bool XDeviceFile_resolvePath(const XString* path, XString* result, XPathStyle style)
{
    if (!path || !result) return false;

    const char* input = XString_toUtf8(path);
    if (!input) return false;

    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return false;

    /* 无卷标且不以根斜杠开始的路径按 FatFs 当前目录解析。 */
    bool hasVolume = XFATFS_platformDrivePrefixToIndex(input) >= 0;
    const char* volumePath = input;
    if (hasVolume) {
        const char* colon = strchr(input, ':');
        volumePath = colon ? colon + 1 : input;
    }
    bool absolute = volumePath[0] == '/' || volumePath[0] == '\\';
    char resolvedFatfs[512];
    char cwd[256];
    if (XFATFS_getCurrentDirectory(cwd, sizeof(cwd)) != FR_OK) return false;
    if (!absolute && !hasVolume) {
        const char* relative = fatfsPath;
        if (relative[0] >= '0' && relative[0] <= '9' && relative[1] == ':' && relative[2] == '/') {
            relative += 3;
        }
        int n = snprintf(resolvedFatfs, sizeof(resolvedFatfs), "%s/%s", cwd, relative);
        if (n < 0 || (size_t)n >= sizeof(resolvedFatfs)) return false;
    } else {
        if (strlen(fatfsPath) >= sizeof(resolvedFatfs)) return false;
        strcpy(resolvedFatfs, fatfsPath);
    }

    /* 转换Fatfs格式 "0:/" → "C:/" */
    char absPath[512] = {0};
    if (resolvedFatfs[0] >= '0' && resolvedFatfs[0] <= '9' && resolvedFatfs[1] == ':') {
        int volIdx = resolvedFatfs[0] - '0';
        if (XFATFS_isVolumeAllocated(volIdx)) {
            XString* xDrive = XString_create();
            if (xDrive && XFATFS_platformDriveAt(volIdx, xDrive)) {
                const char* driveUtf8 = XString_toUtf8(xDrive);
                if (driveUtf8) {
                    snprintf(absPath, sizeof(absPath), "%s%s", driveUtf8, resolvedFatfs + 2);
                }
            }
            if (xDrive) XString_delete_base(xDrive);
            if (absPath[0] == '\0') strcpy(absPath, resolvedFatfs);
        } else {
            strcpy(absPath, resolvedFatfs);
        }
    } else {
        strcpy(absPath, resolvedFatfs);
    }

    XString_assign_utf8(result, absPath);
    (void)style;
    return true;
}
bool XDeviceFile_getSpecialPath(XSpecialPath type, XString* path)
{
    if (!path) return false;
    switch (type) {
    case XSpecialPath_Current: {
        char cwd[256];
        FRESULT fr = XFATFS_getCurrentDirectory(cwd, sizeof(cwd));
        if (fr != FR_OK) return XFATFS_platformCurrentPath(path);
        
        if (cwd[0] >= '0' && cwd[0] <= '9' && cwd[1] == ':') {
            int volIdx = cwd[0] - '0';
            if (XFATFS_isVolumeAllocated(volIdx)) {
                XString* xDrive = XString_create();
                if (xDrive && XFATFS_platformDriveAt(volIdx, xDrive)) {
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
        return XFATFS_platformHomePath(path);
    case XSpecialPath_Root:
        return XFATFS_platformRootPath(path);
    case XSpecialPath_Temp:
        return XFATFS_platformTempPath(path);
    default:
        return false;
    }
}

bool XDeviceFile_setCurrentPath(const XString* path)
{
    if (!path) return false;
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return false;
    FRESULT fr = XFATFS_setCurrentDirectory(fatfsPath);
    if (fr == FR_OK) return true;
    return XFATFS_platformSetCurrentPath(path);
}

/* ============================================================================
 * 七、符号链接操作（2个）- 不支持
 * ============================================================================ */

bool XDeviceFile_link(const XString* targetPath, const XString* linkPath, XLinkType type)
{
    (void)targetPath;
    (void)linkPath;
    (void)type;
    return false;
}

bool XDeviceFile_readLink(const XString* path, XString* target)
{
    (void)path;
    (void)target;
    return false;
}


/* ============================================================================
 * 八、权限操作（1个）- 支持基本只读属性
 * ============================================================================ */

bool XDeviceFile_setPermissions(const XString* path, XFilePermissions permissions)
{
#if FF_FS_READONLY
    (void)path;
    (void)permissions;
    return false;
#else
    if (!path) return false;
    
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return false;
    
    BYTE attr = 0;
    BYTE mask = AM_RDO | AM_HID | AM_SYS | AM_ARC;
    
    /* FatFs 仅有只读属性；只要任一标准写权限位存在，就清除只读。 */
    const XFilePermissions writeMask = XFile_WriteOwner | XFile_WriteUser |
                                       XFile_WriteGroup | XFile_WriteOther;
    if ((permissions & writeMask) == 0) attr |= AM_RDO;
    
    return f_chmod(fatfsPath, attr, mask) == FR_OK;
#endif
}

/* ============================================================================
 * 九、内存映射（3个）- 不支持
 * ============================================================================ */

XFd XDeviceFile_openSharedMemory(const XString* name, bool create, int64_t maxSize, int* error)
{
    /* FatFs 运行在无共享内存的嵌入式环境，命名共享内存段明确不支持 */
    (void)name;
    (void)create;
    (void)maxSize;
    if (error) *error = 0;
    return XFD_INVALID;
}

void* XDeviceFile_map(XFd fd, int64_t offset, int64_t size, int flags)
{
    FIL* fp = XFATFS_getFile(fd);
    if (!fp || offset < 0 || size <= 0 || (uint64_t)size > UINT_MAX) return NULL;

    /* FatFs 没有进程地址空间映射；flags 仅为统一公共接口保留。 */
    (void)flags;
    
    void* buf = XMalloc_System((size_t)size);
    if (!buf) return NULL;
    
    FSIZE_t oldPos = f_tell(fp);
    if (f_lseek(fp, (FSIZE_t)offset) != FR_OK) {
        XFree_System(buf);
        return NULL;
    }
    
    UINT bytesRead;
    if (f_read(fp, buf, (UINT)size, &bytesRead) != FR_OK || bytesRead != (UINT)size) {
        XFree_System(buf);
        f_lseek(fp, oldPos);
        return NULL;
    }
    
    f_lseek(fp, oldPos);
    return buf;
}

bool XDeviceFile_unmap(void* addr, int64_t size)
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
bool XDeviceFile_legacySetFileTime(XFd fd, XFileTime timeType, int64_t timeValue)
{
#if FF_FS_READONLY
    (void)fd;
    (void)timeType;
    (void)timeValue;
    return false;
#else
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
#endif
}

/* ============================================================================
 * 十二、驱动器列表
 * ============================================================================ */

bool XDeviceFile_enumerateDrives(XDeviceFileDriveCallback callback, void* userData)
{
    if (!callback) return false;
    int count = XFATFS_platformDriveCount();
    for (int i = 0; i < count; i++) {
        XString* path = XString_create();
        if (!path) return false;
        bool ok = XFATFS_platformDriveAt(i, path);
        if (!ok) { XString_delete_base(path); return false; }
        bool cont = callback(path, userData);
        XString_delete_base(path);
        if (!cont) return false;
    }
    return true;
}

/* ============================================================================
 * 十三、存储设备信息
 * ============================================================================ */

bool XDeviceFile_getStorageInfo(const XString* path, XStorageInfoData* info)
{
#if FF_FS_READONLY
    (void)path;
    (void)info;
    return false;
#else
    if (!path || !info) return false;
    
    char fatfsPath[512];
    if (!XFATFS_convertPath(path, fatfsPath, sizeof(fatfsPath))) return false;
    
    DWORD freeClusters;
    FATFS* fs = NULL;
    
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
#endif
}

/* ============================================================================
 * 十四、磁盘格式化
 * ============================================================================ */

bool XDeviceFile_format(const XString* drive,
                        XDeviceFileType fsType,
                        const XString* volumeName,
                        int flags,
                        int clusterSize,
                        XDeviceFileFormatProgress progress,
                        void* userData)
{
#if FF_FS_READONLY || !FF_USE_MKFS
    (void)drive;
    (void)fsType;
    (void)volumeName;
    (void)flags;
    (void)clusterSize;
    (void)progress;
    (void)userData;
    return false;
#else
    const char* volumeText;
    int volumeIndex;
    char mountStr[8];
    char fatfsPath[512];
    FATFS* fs = NULL;
    FRESULT fr;
    MKFS_PARM opt;
    BYTE work[FF_MAX_SS];
    bool result = false;

    if (!drive) return false;

    /* FatFS 只提供 FAT 系列格式；拒绝会被静默忽略的扩展选项。 */
    if (flags & (XFileSystemFormat_Compress | XFileSystemFormat_Encrypt)) return false;
    volumeText = volumeName ? XString_toUtf8(volumeName) : NULL;
#if !FF_USE_LABEL
    if (volumeText && volumeText[0] != '\0') return false;
#endif

    if (!XFATFS_convertPath(drive, fatfsPath, sizeof(fatfsPath))) return false;

    /* convertPath 已确保卷号存在；格式化前必须卸载，否则 FatFS 仍会使用旧 BPB。 */
    {
        char* colon = strchr(fatfsPath, ':');
        char* end = NULL;
        long parsed;
        if (!colon || colon == fatfsPath) return false;
        parsed = strtol(fatfsPath, &end, 10);
        if (end != colon || parsed < 0 || parsed >= FF_VOLUMES) return false;
        volumeIndex = (int)parsed;
    }
    if (!XFATFS_makeMountString(volumeIndex, mountStr, sizeof(mountStr))) return false;
    if (!XFATFS_lockState()) return false;
    if (volumeIndex >= FF_VOLUMES || !g_volumes[volumeIndex]) goto format_done;
    fs = g_volumes[volumeIndex];
    if (progress && !progress(0, userData)) goto format_done;
    f_mount(NULL, mountStr, 0);
    g_mountedMask &= ~(1u << volumeIndex);
    memset(fs, 0, sizeof(*fs));

    memset(&opt, 0, sizeof(opt));
    opt.fmt = FM_ANY;
    if (fsType == XDeviceFileType_FAT32) opt.fmt = FM_FAT32;
    else if (fsType == XDeviceFileType_exFAT) opt.fmt = FM_EXFAT;
    if (clusterSize > 0) opt.au_size = (DWORD)clusterSize;
    if (progress && !progress(50, userData)) goto format_done;
    fr = f_mkfs(mountStr, &opt, work, sizeof(work));
    if (fr != FR_OK) goto format_done;

    fr = f_mount(fs, mountStr, 1);
    if (fr != FR_OK) goto format_done;
    g_mountedMask |= (1u << volumeIndex);
#if FF_USE_LABEL
    if (volumeText && volumeText[0] != '\0') {
        fr = f_setlabel((const TCHAR*)volumeText);
        if (fr != FR_OK) goto format_done;
    }
#else
    (void)volumeText;
#endif
    if (progress && !progress(100, userData)) goto format_done;
    result = true;

format_done:
    /* 格式化前已卸载卷时，取消或失败也尽量恢复原有挂载状态。 */
    if (!result && fs && !(g_mountedMask & (1u << volumeIndex))) {
        if (f_mount(fs, mountStr, 1) == FR_OK)
            g_mountedMask |= (1u << volumeIndex);
    }
    XFATFS_unlockState();
    return result;
#endif
}

#endif /* XFILE_USE_FATFS */
