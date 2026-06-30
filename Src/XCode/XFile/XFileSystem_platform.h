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
 * API 分类统计：
 * ============================================================================
 * 
 * 一、核心文件操作（8个）- 必需实现
 * 二、文件属性操作（2个）- 必需实现
 * 三、文件系统操作（4个）- 必需实现
 * 四、目录操作（5个）- 必需实现
 * 五、路径操作（1个）- 必需实现
 * 六、特殊路径（5个）
 * 七、符号链接操作（2个）- 可选
 * 八、权限操作（1个）- 可选
 * 九、内存映射（2个）- 可选
 * 十、递归删除目录（1个）- 可选
 * 十一、文件时间修改（1个）- 可选
 * 十二、驱动器列表（1个）- 可选
 * 十三、存储设备信息（1个）- 可选
 * 十四、磁盘格式化（1个）- 可选
 */

#ifndef XFILESYSTEM_PLATFORM_H
#define XFILESYSTEM_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
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
 * 一、核心文件操作（8个）- 必需实现
 * ============================================================================ */

/**
 * @brief 打开文件
 * @param path 文件路径（XString）
 * @param mode 打开模式（XFileSystemOpenMode组合）
 * @param error 输出错误码（可为NULL）
 * @return 文件描述符，失败返回-1
 */
int XFileSystem_open(const XString* path, int mode, int* error);

/**
 * @brief 关闭文件
 * @param fd 文件描述符
 */
void XFileSystem_close(int fd);

/**
 * @brief 获取文件当前位置
 * @param fd 文件描述符
 * @return 当前位置，失败返回-1
 */
int64_t XFileSystem_pos(int fd);

/**
 * @brief 移动文件指针
 * @param fd 文件描述符
 * @param pos 目标位置
 * @return 成功返回true
 */
bool XFileSystem_seek(int fd, int64_t pos);

/**
 * @brief 读取文件数据
 * @param fd 文件描述符
 * @param buf 目标缓冲区
 * @param len 最大读取长度
 * @return 实际读取字节数，-1表示错误
 */
int64_t XFileSystem_read(int fd, void* buf, int64_t len);

/**
 * @brief 写入文件数据
 * @param fd 文件描述符
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @return 实际写入字节数，-1表示错误
 */
int64_t XFileSystem_write(int fd, const void* buf, int64_t len);

/**
 * @brief 刷新文件缓冲区
 * @param fd 文件描述符
 * @return 成功返回true
 */
bool XFileSystem_flush(int fd);

/**
 * @brief 调整文件大小
 * @param fd 文件描述符
 * @param size 新大小
 * @return 成功返回true
 */
bool XFileSystem_resize(int fd, int64_t size);

/* ============================================================================
 * 二、文件属性操作（2个）- 必需实现
 * ============================================================================ */

/**
 * @brief 获取文件属性（通过路径）
 * @param path 文件路径
 * @param stat 输出文件属性结构
 * @return 成功返回true
 */
bool XFileSystem_stat(const XString* path, XFileStat* stat);

/**
 * @brief 获取文件属性（通过句柄）
 * @param fd 文件描述符
 * @param stat 输出文件属性结构
 * @return 成功返回true
 */
bool XFileSystem_fstat(int fd, XFileStat* stat);

/* ============================================================================
 * 三、文件系统操作（4个）- 必需实现
 * ============================================================================ */

/**
 * @brief 检查文件是否存在
 * @param path 文件路径
 * @return 存在返回true
 */
bool XFileSystem_exists(const XString* path);

/**
 * @brief 删除文件
 * @param path 文件路径
 * @return 成功返回true
 */
bool XFileSystem_remove(const XString* path);

/**
 * @brief 重命名文件
 * @param oldPath 原路径
 * @param newPath 新路径
 * @return 成功返回true
 */
bool XFileSystem_rename(const XString* oldPath, const XString* newPath);

/**
 * @brief 复制文件
 * @param srcPath 源路径
 * @param dstPath 目标路径
 * @return 成功返回true
 */
bool XFileSystem_copy(const XString* srcPath, const XString* dstPath);

/* ============================================================================
 * 四、目录操作（5个）- 必需实现
 * ============================================================================ */

/**
 * @brief 创建目录
 * @param path 目录路径
 * @param recursive 是否递归创建父目录
 * @return 成功返回true
 */
bool XFileSystem_mkdir(const XString* path, bool recursive);

/**
 * @brief 删除空目录
 * @param path 目录路径
 * @return 成功返回true
 */
bool XFileSystem_rmdir(const XString* path);

/**
 * @brief 打开目录迭代器
 * @param path 目录路径
 * @return 迭代器句柄，失败返回NULL
 */
XDirIterator XFileSystem_opendir(const XString* path);

/**
 * @brief 读取下一个目录项
 * @param iter 迭代器句柄
 * @param entry 输出目录项信息（name字段需预先创建XString）
 * @return 成功返回true，枚举结束返回false
 */
bool XFileSystem_readdir(XDirIterator iter, XDirEntry* entry);

/**
 * @brief 关闭目录迭代器
 * @param iter 迭代器句柄
 */
void XFileSystem_closedir(XDirIterator iter);

/* ============================================================================
 * 五、路径操作（1个）- 必需实现
 * ============================================================================ */

/**
 * @brief 解析路径
 * @param path 原始路径
 * @param result 输出XString（需调用者预先创建）
 * @param style 解析风格（绝对路径或规范化路径）
 * @return 成功返回true
 */
bool XFileSystem_resolvePath(const XString* path, XString* result, XPathStyle style);

/* ============================================================================
 * 六、特殊路径（5个）
 * ============================================================================ */

/**
 * @brief 获取当前工作目录
 * @param path 输出XString（需调用者预先创建）
 * @return 成功返回true
 */
bool XFileSystem_currentPath(XString* path);

/**
 * @brief 设置当前工作目录
 * @param path 目标路径
 * @return 成功返回true
 */
bool XFileSystem_setCurrentPath(const XString* path);

/**
 * @brief 获取用户主目录
 * @param path 输出XString（需调用者预先创建）
 * @return 成功返回true
 */
bool XFileSystem_homePath(XString* path);

/**
 * @brief 获取根目录
 * @param path 输出XString（需调用者预先创建）
 * @return 成功返回true
 */
bool XFileSystem_rootPath(XString* path);

/**
 * @brief 获取临时目录
 * @param path 输出XString（需调用者预先创建）
 * @return 成功返回true
 */
bool XFileSystem_tempPath(XString* path);

/* ============================================================================
 * 七、符号链接操作（2个）- 可选
 * ============================================================================ */

/**
 * @brief 创建链接
 * @param targetPath 目标路径
 * @param linkPath 链接路径
 * @return 成功返回true
 */
bool XFileSystem_link(const XString* targetPath, const XString* linkPath);

/**
 * @brief 读取符号链接目标
 * @param path 符号链接路径
 * @param target 输出XString（需调用者预先创建）
 * @return 成功返回true
 */
bool XFileSystem_readLink(const XString* path, XString* target);

/* ============================================================================
 * 八、权限操作（1个）- 可选
 * ============================================================================ */

/**
 * @brief 设置文件权限
 * @param path 文件路径
 * @param permissions 权限位掩码（XFilePermissions）
 * @return 成功返回true
 */
bool XFileSystem_setPermissions(const XString* path, XFilePermissions permissions);

/* ============================================================================
 * 九、内存映射（2个）- 可选
 * ============================================================================ */

/**
 * @brief 内存映射文件
 * @param fd 文件描述符
 * @param offset 映射起始偏移
 * @param size 映射大小
 * @param writable 是否可写
 * @return 映射地址，失败返回NULL
 */
void* XFileSystem_map(int fd, int64_t offset, int64_t size, bool writable);

/**
 * @brief 取消内存映射
 * @param addr 映射地址
 * @param size 映射大小
 * @return 成功返回true
 */
bool XFileSystem_unmap(void* addr, int64_t size);

/* ============================================================================
 * 十、递归删除目录（1个）- 可选
 * ============================================================================ */

/**
 * @brief 递归删除目录及其内容
 * @param path 目录路径
 * @return 成功返回true
 */
bool XFileSystem_rmdir_recursive(const XString* path);

/* ============================================================================
 * 十一、文件时间修改（1个）- 可选
 * ============================================================================ */

/**
 * @brief 设置文件时间
 * @param fd 文件描述符
 * @param timeType 时间类型（访问时间/修改时间/创建时间）
 * @param timeValue 时间值（Unix时间戳，毫秒）
 * @return 成功返回true
 */
bool XFileSystem_setFileTime(int fd, XFileTime timeType, int64_t timeValue);

/* ============================================================================
 * 十二、驱动器列表（1个）- 可选
 * ============================================================================ */

/**
 * @brief 获取驱动器/挂载点数量
 * @return 驱动器数量，失败返回0
 */
int XFileSystem_drives_count(void);

/**
 * @brief 获取驱动器/挂载点路径
 * @param index 驱动器索引（0 ~ count-1）
 * @param path 输出XString（需调用者预先创建）
 * @return 成功返回true
 */
bool XFileSystem_drives_at(int index, XString* path);

/* ============================================================================
 * 十三、存储设备信息（1个）- 可选
 * ============================================================================ */

/**
 * @brief 获取完整的存储设备信息
 * @param path 挂载点路径
 * @param info 输出存储设备信息结构体（所有XString指针需预先创建）
 * @return 成功返回 true
 * @note 一次性获取所有存储信息，减少系统调用次数
 *       XStorageInfoData 定义在 XStorageInfo.h 中
 */
bool XFileSystem_getStorageInfo(const XString* path, XStorageInfoData* info);

/* ============================================================================
 * 十四、磁盘格式化（1个）- 可选
 * ============================================================================ */

/**
 * @brief 格式化选项标志
 */
typedef enum XFileSystemFormatFlags {
    XFileSystemFormat_None       = 0,     /**< 默认选项 */
    XFileSystemFormat_Quick      = 0x01,  /**< 快速格式化 */
    XFileSystemFormat_Force      = 0x02,  /**< 强制格式化（即使正在使用） */
    XFileSystemFormat_Compress   = 0x04,  /**< 启用压缩（NTFS） */
    XFileSystemFormat_Encrypt    = 0x08,  /**< 启用加密（NTFS） */
} XFileSystemFormatFlags;

/**
 * @brief 文件系统类型（格式化用）
 */
typedef enum XFileSystemType {
    XFileSystemType_Auto,    /**< 自动选择（推荐） */
    XFileSystemType_FAT32,   /**< FAT32 文件系统 */
    XFileSystemType_NTFS,    /**< NTFS 文件系统 */
    XFileSystemType_exFAT,   /**< exFAT 文件系统 */
    XFileSystemType_EXT4,    /**< EXT4 文件系统（Linux） */
    XFileSystemType_F2FS,    /**< F2FS 文件系统（Linux，适合Flash） */
} XFileSystemType;

/**
 * @brief 格式化进度回调函数
 * @param progress 进度百分比（0-100）
 * @param userData 用户数据指针
 * @return 返回true继续格式化，返回false取消格式化
 */
typedef bool (*XFileSystemFormatProgress)(int progress, void* userData);

/**
 * @brief 格式化磁盘
 * @param drive 驱动器路径（如 "C:" 或 "/dev/sda1"）
 * @param fsType 文件系统类型
 * @param volumeName 卷标名称（可为NULL）
 * @param flags 格式化选项
 * @param clusterSize 簇大小（0表示默认，否则为字节数）
 * @param progress 进度回调（可为NULL）
 * @param userData 进度回调用户数据
 * @return 成功返回true
 * @note 平台层需实现：
 *       - Windows: FormatEx 或 SHFormatDrive
 *       - Linux: mkfs 命令
 *       此操作需要管理员权限
 */
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