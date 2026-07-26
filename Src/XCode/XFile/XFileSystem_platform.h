/**
 * @file XFileSystem_platform.h
 * @brief 平台相关文件系统操作统一接口
 * 
 * 设计说明：
 * - 所有路径参数使用 XString 类型
 * - 所有输出字符串使用 XString 类型
 * - XFileStat 是纯数据结构，供平台层返回文件属性
 * 
 * ============================================================================
 * API 分类统计（共31个平台函数 + 1个内联便捷函数）：
 * ============================================================================
 * 
 * 一、核心文件操作（8个）- 必需实现
 * 二、文件属性操作（2个）- 必需实现（+ XFileSystem_exists 内联便捷函数）
 * 三、文件系统操作（3个）- 必需实现
 * 四、目录操作（5个）- 必需实现（rmdir 合并了递归删除）
 * 五、路径操作（1个）- 必需实现
 * 六、特殊路径（2个）- 合并了5个路径函数为 getSpecialPath + setCurrentPath
 * 七、符号链接操作（2个）- 可选
 * 八、权限操作（1个）- 可选
 * 九、内存映射（2个）- 可选
 * 十、文件时间修改（1个）- 仅fd版，路径版由上层 open→setFileTime→close 组合
 * 十一、驱动器列表（2个）- 可选
 * 十二、存储设备信息（1个）- 可选
 * 十三、磁盘格式化（1个）- 可选
 */

#ifndef XFILESYSTEM_PLATFORM_H
#define XFILESYSTEM_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include "XDateTime.h"
#include "XFileInfo.h"
#include "XFileSystem_config.h"
#include "XStorageInfo.h"  /* XStorageInfoData定义 */

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
struct XString;
typedef struct XString XString;

/* ============================================================================
 * 目录迭代器
 * ============================================================================ */

typedef struct XDirEntry {
    XString* name;              /**< 文件名（需调用者创建，平台层赋值） */
    uint8_t isDir       : 1;    /**< 是否为目录 */
    uint8_t isFile      : 1;    /**< 是否为普通文件 */
    uint8_t isSymLink   : 1;    /**< 是否为符号链接 */
    uint8_t isHidden    : 1;    /**< 是否为隐藏文件 */
    uint8_t _reserved   : 4;    /**< 保留位 */
} XDirEntry;

typedef void* XDirIterator;

/* ============================================================================
 * 路径解析风格
 * ============================================================================ */

typedef enum {
    XPathStyle_Absolute,    /**< 绝对路径（不解析符号链接） */
    XPathStyle_Canonical    /**< 规范化路径（解析符号链接） */
} XPathStyle;

/* ============================================================================
 * 特殊路径类型
 * ============================================================================ */

typedef enum {
    XSpecialPath_Current,   /**< 当前工作目录 */
    XSpecialPath_Home,      /**< 用户主目录 */
    XSpecialPath_Root,      /**< 根目录 */
    XSpecialPath_Temp,      /**< 临时目录 */
} XSpecialPath;

/* ============================================================================
 * 一、核心文件操作（8个）- 必需实现
 * ============================================================================ */

XFd XFileSystem_open(const XString* path, int mode, int* error);
void XFileSystem_close(XFd fd);
int64_t XFileSystem_pos(XFd fd);
bool XFileSystem_seek(XFd fd, int64_t pos);
int64_t XFileSystem_read(XFd fd, void* buf, int64_t len);
int64_t XFileSystem_write(XFd fd, const void* buf, int64_t len);
bool XFileSystem_flush(XFd fd);
bool XFileSystem_resize(XFd fd, int64_t size);

/* ============================================================================
 * 二、文件属性操作（2个）- 必需实现
 * ============================================================================ */

bool XFileSystem_stat(const XString* path, XFileStat* stat);
bool XFileSystem_fstat(XFd fd, XFileStat* stat);

/**
 * @brief 检查文件是否存在（便捷内联函数，等价于 stat + stat.exists）
 */
static inline bool XFileSystem_exists(const XString* path) {
    XFileStat st;
    return XFileSystem_stat(path, &st) && st.exists;
}

/* ============================================================================
 * 三、文件系统操作（3个）- 必需实现
 * ============================================================================ */

bool XFileSystem_remove(const XString* path);
bool XFileSystem_rename(const XString* oldPath, const XString* newPath);
bool XFileSystem_copy(const XString* srcPath, const XString* dstPath);

/* ============================================================================
 * 四、目录操作（5个）- 必需实现
 * ============================================================================ */

bool XFileSystem_mkdir(const XString* path, bool recursive);

/**
 * @brief 删除目录
 * @param path 目录路径
 * @param recursive true=递归删除目录及其内容，false=仅删除空目录
 * @return 成功返回true
 */
bool XFileSystem_rmdir(const XString* path, bool recursive);

XDirIterator XFileSystem_opendir(const XString* path);
bool XFileSystem_readdir(XDirIterator iter, XDirEntry* entry);
void XFileSystem_closedir(XDirIterator iter);

/* ============================================================================
 * 五、路径操作（1个）- 必需实现
 * ============================================================================ */

bool XFileSystem_resolvePath(const XString* path, XString* result, XPathStyle style);

/* ============================================================================
 * 六、特殊路径（2个）
 * ============================================================================ */

/**
 * @brief 获取特殊目录路径
 * @param type 特殊路径类型
 * @param path 输出XString（需调用者预先创建）
 * @return 成功返回true
 */
bool XFileSystem_getSpecialPath(XSpecialPath type, XString* path);

/**
 * @brief 设置当前工作目录
 * @param path 新的当前目录路径
 * @return 成功返回true
 */
bool XFileSystem_setCurrentPath(const XString* path);

/* ============================================================================
 * 七、符号链接操作（2个）- 可选
 * ============================================================================ */

bool XFileSystem_link(const XString* targetPath, const XString* linkPath);
bool XFileSystem_readLink(const XString* path, XString* target);

/* ============================================================================
 * 八、权限操作（1个）- 可选
 * ============================================================================ */

bool XFileSystem_setPermissions(const XString* path, XFilePermissions permissions);

/* ============================================================================
 * 九、内存映射（2个）- 可选
 * ============================================================================ */

/**
 * @brief 内存映射
 * @param fd XFd 描述符
 * @param offset 偏移
 * @param size 大小
 * @param flags XFileDeviceMemoryMapFlags (bit0=MapPrivateOption, bit1=视为可写)
 */
void* XFileSystem_map(XFd fd, int64_t offset, int64_t size, int flags);
bool XFileSystem_unmap(void* addr, int64_t size);

/* ============================================================================
 * 十、文件时间修改（1个）- 可选
 * ============================================================================ */

/**
 * @brief 通过文件描述符设置文件时间
 * @param fd 文件描述符（XFileDescriptor 表索引）
 * @param timeType 时间类型（访问时间/修改时间/创建时间）
 * @param timeValue 时间值（Unix时间戳，秒）
 * @return 成功返回true
 * @note 直接操作已打开的句柄。Win32 用 SetFileTime(HANDLE)，
 *       POSIX 用 futimens，FatFs 通过句柄内存储的路径调用 f_utime。
 *       路径版需求由上层通过 open→setFileTime→close 组合实现。
 */
bool XFileSystem_setFileTime(XFd fd, XFileTime timeType, int64_t timeValue);

/* ============================================================================
 * 十一、驱动器列表（2个）- 可选
 * ============================================================================ */

int XFileSystem_drives_count(void);
bool XFileSystem_drives_at(int index, XString* path);

/* ============================================================================
 * 十二、存储设备信息（1个）- 可选
 * ============================================================================ */

bool XFileSystem_getStorageInfo(const XString* path, XStorageInfoData* info);

/**
 * @brief 将文件移动到回收站（XDG Trash / Windows Shell / macOS Trash）
 * @param fileName 源文件路径
 * @param pathInTrash 回收站中的目标路径（可选，输出）
 * @return 成功返回 true，失败返回 false
 * @note POSIX 端按 FreeDesktop Trash 规范实现；Windows 端调用 SHFileOperationW；
 *       若平台不可用可退化为直接 unlink/DeleteFile。
 *       这与 XFileSystem_remove (XFile::remove) 的语义不同:
 *         remove     = 立即从文件系统中删除, 不可恢复
 *         moveToTrash = 移动到 OS 回收站, 用户可恢复
 */
bool XFileSystem_moveToTrash(const XString* fileName, XString* pathInTrash);

/* ============================================================================
 * 十三、磁盘格式化（1个）- 可选
 * ============================================================================ */

typedef enum XFileSystemFormatFlags {
    XFileSystemFormat_None       = 0,
    XFileSystemFormat_Quick      = 0x01,
    XFileSystemFormat_Force      = 0x02,
    XFileSystemFormat_Compress   = 0x04,
    XFileSystemFormat_Encrypt    = 0x08,
} XFileSystemFormatFlags;

typedef enum XFileSystemType {
    XFileSystemType_Auto,
    XFileSystemType_FAT32,
    XFileSystemType_NTFS,
    XFileSystemType_exFAT,
    XFileSystemType_EXT4,
    XFileSystemType_F2FS,
} XFileSystemType;

typedef bool (*XFileSystemFormatProgress)(int progress, void* userData);

bool XFileSystem_format(const XString* drive, 
                        XFileSystemType fsType,
                        const XString* volumeName,
                        int flags,
                        int clusterSize,
                        XFileSystemFormatProgress progress,
                        void* userData);

#ifdef __cplusplus
}
#endif

#endif // XFILESYSTEM_PLATFORM_H
