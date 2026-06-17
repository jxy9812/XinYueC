/**
 * @file XFileSystem_platform.h
 * @brief 平台相关文件系统操作统一接口
 * 
 * 这个头文件定义了所有平台需要实现的文件系统操作函数。
 * 每个平台只需要实现这一个文件中的函数即可完成所有文件系统功能。
 * 
 * 优化目标：
 * - 减少平台函数数量（从 ~50 个减少到 ~15 个）
 * - 消除重复代码
 * - 简化跨平台移植工作
 */

#ifndef XFILESYSTEM_PLATFORM_H
#define XFILESYSTEM_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include "XFileDevice.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 文件打开模式标志
 * ============================================================================ */

typedef enum XFileSystemOpenMode {
    XFileSystem_ReadOnly  = 0x0001,  /**< 只读 */
    XFileSystem_WriteOnly = 0x0002,  /**< 只写 */
    XFileSystem_ReadWrite = 0x0003,  /**< 读写 */
    XFileSystem_Append    = 0x0004,  /**< 追加 */
    XFileSystem_Truncate  = 0x0008,  /**< 截断 */
    XFileSystem_Create    = 0x0010,  /**< 创建（不存在则创建） */
    XFileSystem_NewOnly   = 0x0020,  /**< 仅新建（存在则失败） */
    XFileSystem_Existing  = 0x0040,  /**< 仅存在（不存在则失败） */
    XFileSystem_Text      = 0x0080   /**< 文本模式 */
} XFileSystemOpenMode;

/* ============================================================================
 * 文件属性结构体
 * ============================================================================ */

/**
 * @brief 文件属性缓存结构体
 * 
 * 一次系统调用获取所有文件属性，避免多次调用
 */
typedef struct XFileStat {
    /* 基本信息 */
    int64_t size;               /**< 文件大小（字节） */
    
    /* 时间信息 (Unix 时间戳，秒) */
    int64_t birthTime;          /**< 创建时间 */
    int64_t modificationTime;   /**< 修改时间 */
    int64_t accessTime;         /**< 访问时间 */
    int64_t metadataChangeTime; /**< 元数据修改时间 */
    
    /* 权限 */
    uint32_t permissions;       /**< 权限标志 */
    
    /* 所有者 */
    uint32_t ownerId;           /**< 所有者ID */
    uint32_t groupId;           /**< 组ID */
    
    /* 类型标志 */
    bool exists;                /**< 是否存在 */
    bool isFile;                /**< 是否为普通文件 */
    bool isDir;                 /**< 是否为目录 */
    bool isSymLink;             /**< 是否为符号链接 */
    bool isHidden;              /**< 是否为隐藏 */
    bool isReadable;            /**< 是否可读 */
    bool isWritable;            /**< 是否可写 */
    bool isExecutable;          /**< 是否可执行 */
} XFileStat;

/* ============================================================================
 * 核心文件操作（文件句柄相关）
 * ============================================================================ */

/**
 * @brief 打开文件
 * @param path 文件路径（UTF-8 编码）
 * @param mode 打开模式
 * @param error 错误码输出（可为 NULL）
 * @return 文件句柄，失败返回 -1
 */
int XFileSystem_open(const char* path, int mode, int* error);

/**
 * @brief 关闭文件
 * @param fd 文件句柄
 */
void XFileSystem_close(int fd);

/**
 * @brief 获取当前位置
 * @param fd 文件句柄
 * @return 当前位置，失败返回 -1
 */
int64_t XFileSystem_pos(int fd);

/**
 * @brief 定位文件指针
 * @param fd 文件句柄
 * @param pos 目标位置
 * @return 成功返回 true
 */
bool XFileSystem_seek(int fd, int64_t pos);

/**
 * @brief 读取数据
 * @param fd 文件句柄
 * @param buf 缓冲区
 * @param len 最大长度
 * @return 实际读取字节数，失败返回 -1
 */
int64_t XFileSystem_read(int fd, void* buf, int64_t len);

/**
 * @brief 写入数据
 * @param fd 文件句柄
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @return 实际写入字节数，失败返回 -1
 */
int64_t XFileSystem_write(int fd, const void* buf, int64_t len);

/**
 * @brief 刷新缓冲区
 * @param fd 文件句柄
 * @return 成功返回 true
 */
bool XFileSystem_flush(int fd);

/**
 * @brief 调整文件大小
 * @param fd 文件句柄
 * @param size 新大小
 * @return 成功返回 true
 */
bool XFileSystem_resize(int fd, int64_t size);

/* ============================================================================
 * 文件属性操作
 * ============================================================================ */

/**
 * @brief 获取文件属性（通过路径）
 * @param path 文件路径（UTF-8 编码）
 * @param stat 属性结构体输出
 * @return 成功返回 true
 */
bool XFileSystem_stat(const char* path, XFileStat* stat);

/**
 * @brief 获取文件属性（通过句柄）
 * @param fd 文件句柄
 * @param stat 属性结构体输出
 * @return 成功返回 true
 */
bool XFileSystem_fstat(int fd, XFileStat* stat);

/**
 * @brief 获取文件大小（通过句柄，快捷函数）
 * @param fd 文件句柄
 * @return 文件大小，失败返回 -1
 */
int64_t XFileSystem_size(int fd);

/* ============================================================================
 * 文件系统操作（路径相关）
 * ============================================================================ */

/**
 * @brief 检查文件是否存在
 * @param path 文件路径（UTF-8 编码）
 * @return 存在返回 true
 */
bool XFileSystem_exists(const char* path);

/**
 * @brief 删除文件
 * @param path 文件路径（UTF-8 编码）
 * @return 成功返回 true
 */
bool XFileSystem_remove(const char* path);

/**
 * @brief 重命名文件
 * @param oldPath 原路径
 * @param newPath 新路径
 * @return 成功返回 true
 */
bool XFileSystem_rename(const char* oldPath, const char* newPath);

/**
 * @brief 复制文件
 * @param srcPath 源路径
 * @param dstPath 目标路径
 * @return 成功返回 true
 */
bool XFileSystem_copy(const char* srcPath, const char* dstPath);

/**
 * @brief 创建链接
 * @param targetPath 目标路径
 * @param linkPath 链接路径
 * @return 成功返回 true
 */
bool XFileSystem_link(const char* targetPath, const char* linkPath);

/**
 * @brief 移动到回收站
 * @param path 文件路径
 * @return 成功返回 true
 */
bool XFileSystem_moveToTrash(const char* path);

/**
 * @brief 获取符号链接目标
 * @param path 符号链接路径
 * @param target 目标路径输出缓冲区
 * @param targetSize 缓冲区大小
 * @return 成功返回 true
 */
bool XFileSystem_readLink(const char* path, char* target, int targetSize);

/**
 * @brief 设置文件权限
 * @param path 文件路径
 * @param permissions 权限标志
 * @return 成功返回 true
 */
bool XFileSystem_setPermissions(const char* path, uint32_t permissions);

/**
 * @brief 设置文件时间
 * @param fd 文件句柄
 * @param timeType 时间类型
 * @param timeValue 时间值（Unix 时间戳）
 * @return 成功返回 true
 */
bool XFileSystem_setFileTime(int fd, int timeType, int64_t timeValue);

/* ============================================================================
 * 内存映射
 * ============================================================================ */

/**
 * @brief 内存映射
 * @param fd 文件句柄
 * @param offset 偏移量
 * @param size 大小
 * @param writable 是否可写
 * @return 映射地址，失败返回 NULL
 */
void* XFileSystem_map(int fd, int64_t offset, int64_t size, bool writable);

/**
 * @brief 取消映射
 * @param addr 映射地址
 * @param size 大小
 * @return 成功返回 true
 */
bool XFileSystem_unmap(void* addr, int64_t size);

/* ============================================================================
 * 路径操作
 * ============================================================================ */

/**
 * @brief 获取绝对路径
 * @param path 相对路径
 * @param absPath 绝对路径输出缓冲区
 * @param absPathSize 缓冲区大小
 * @return 成功返回 true
 */
bool XFileSystem_absolutePath(const char* path, char* absPath, int absPathSize);

#ifdef __cplusplus
}
#endif

#endif // XFILESYSTEM_PLATFORM_H