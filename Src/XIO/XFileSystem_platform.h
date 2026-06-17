/**
 * @file XFileSystem_platform.h
 * @brief 平台相关文件系统操作统一接口（完整版，对齐Qt）
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
    XFileSystem_ReadOnly  = 0x0001,
    XFileSystem_WriteOnly = 0x0002,
    XFileSystem_ReadWrite = 0x0003,
    XFileSystem_Append    = 0x0004,
    XFileSystem_Truncate  = 0x0008,
    XFileSystem_Create    = 0x0010,
    XFileSystem_NewOnly   = 0x0020,
    XFileSystem_Existing  = 0x0040,
    XFileSystem_Text      = 0x0080
} XFileSystemOpenMode;

/* ============================================================================
 * 文件属性结构体（扩展版）
 * ============================================================================ */

typedef struct XFileStat {
    /* 基本信息 */
    int64_t size;
    
    /* 时间信息 (Unix 时间戳，秒) */
    int64_t birthTime;
    int64_t modificationTime;
    int64_t accessTime;
    int64_t metadataChangeTime;
    
    /* 权限 */
    uint32_t permissions;
    
    /* 所有者 */
    uint32_t ownerId;
    uint32_t groupId;
    
    /* 类型标志 */
    bool exists;
    bool isFile;
    bool isDir;
    bool isSymLink;
    bool isHidden;
    bool isReadable;
    bool isWritable;
    bool isExecutable;
    
    /* Windows 特有 */
    bool isJunction;
    bool isShortcut;
} XFileStat;

/* ============================================================================
 * 目录迭代器
 * ============================================================================ */

typedef struct XDirEntry {
    char name[256];
    bool isDir;
    bool isFile;
    bool isSymLink;
    bool isHidden;
} XDirEntry;

typedef void* XDirIterator;

/* ============================================================================
 * 核心文件操作
 * ============================================================================ */

int XFileSystem_open(const char* path, int mode, int* error);
void XFileSystem_close(int fd);
int64_t XFileSystem_pos(int fd);
bool XFileSystem_seek(int fd, int64_t pos);
int64_t XFileSystem_read(int fd, void* buf, int64_t len);
int64_t XFileSystem_write(int fd, const void* buf, int64_t len);
bool XFileSystem_flush(int fd);
bool XFileSystem_resize(int fd, int64_t size);

/* ============================================================================
 * 文件属性操作
 * ============================================================================ */

bool XFileSystem_stat(const char* path, XFileStat* stat);
bool XFileSystem_fstat(int fd, XFileStat* stat);
int64_t XFileSystem_size(int fd);

/* ============================================================================
 * 文件系统操作
 * ============================================================================ */

bool XFileSystem_exists(const char* path);
bool XFileSystem_remove(const char* path);
bool XFileSystem_rename(const char* oldPath, const char* newPath);
bool XFileSystem_copy(const char* srcPath, const char* dstPath);
bool XFileSystem_link(const char* targetPath, const char* linkPath);
bool XFileSystem_moveToTrash(const char* path);
bool XFileSystem_readLink(const char* path, char* target, int targetSize);
bool XFileSystem_setPermissions(const char* path, uint32_t permissions);
bool XFileSystem_setFileTime(int fd, int timeType, int64_t timeValue);

/* ============================================================================
 * 目录操作
 * ============================================================================ */

bool XFileSystem_mkdir(const char* path);
bool XFileSystem_rmdir(const char* path);
bool XFileSystem_mkdir_p(const char* path);  // 创建多级目录
XDirIterator XFileSystem_opendir(const char* path);
bool XFileSystem_readdir(XDirIterator iter, XDirEntry* entry);
void XFileSystem_closedir(XDirIterator iter);

/* ============================================================================
 * 内存映射
 * ============================================================================ */

void* XFileSystem_map(int fd, int64_t offset, int64_t size, bool writable);
bool XFileSystem_unmap(void* addr, int64_t size);

/* ============================================================================
 * 路径操作
 * ============================================================================ */

bool XFileSystem_absolutePath(const char* path, char* absPath, int absPathSize);
bool XFileSystem_canonicalPath(const char* path, char* canPath, int canPathSize);

/* ============================================================================
 * 所有者操作
 * ============================================================================ */

bool XFileSystem_getOwner(const char* path, char* owner, int ownerSize, uint32_t* ownerId);
bool XFileSystem_getGroup(const char* path, char* group, int groupSize, uint32_t* groupId);
bool XFileSystem_setOwner(const char* path, uint32_t ownerId);
bool XFileSystem_setGroup(const char* path, uint32_t groupId);

/* ============================================================================
 * 符号链接高级操作
 * ============================================================================ */

bool XFileSystem_isJunction(const char* path);
bool XFileSystem_isShortcut(const char* path, char* target, int targetSize);
bool XFileSystem_junctionTarget(const char* path, char* target, int targetSize);

/* ============================================================================
 * 文件系统信息
 * ============================================================================ */

int64_t XFileSystem_totalSpace(const char* path);
int64_t XFileSystem_availableSpace(const char* path);

#ifdef __cplusplus
}
#endif

#endif // XFILESYSTEM_PLATFORM_H