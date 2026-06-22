/**
 * @file XFileSystem_platform.h
 * @brief 平台相关文件系统操作统一接口（优化版）
 * 
 * API数量：30个（从33个优化，减少3个）
 * 
 * 优化内容：
 * - 合并 mkdir/mkdir_p → mkdir(path, recursive) [减少1个]
 * - 合并 absolutePath/canonicalPath → resolvePath(path, result, size, style) [减少1个]
 * - 移除 XFileSystem_size（可用fstat替代）[减少1个]
 * 
 * 设计说明：
 * - XFileStat 是纯数据结构，供平台层返回文件属性
 * - XFileInfo（应用层）包含 XFileStat 作为成员，避免字段重复
 * - rootPath/tempPath 保留在平台层，因为应用层不能使用平台特定API
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
 * 
 * ============================================================================
 * 模块依赖关系：
 * ============================================================================
 * 
 * XFile 使用：10个API
 *   - open, close, pos, seek, read, write, flush, resize
 *   - fstat（获取文件大小）
 * 
 * XFileInfo 使用：3个API
 *   - stat, readLink（符号链接）, exists
 * 
 * XDir 使用：4个API
 *   - opendir, readdir, closedir, exists
 * 
 * ============================================================================
 * 移植优先级：
 * ============================================================================
 * 
 * 必需实现（20个）：
 *   - 核心文件操作：8个
 *   - 文件属性操作：2个
 *   - 文件系统操作：4个
 *   - 目录操作：5个
 *   - 路径操作：1个
 * 
 * 建议实现（5个）：
 *   - 特殊路径：5个（应用层常用）
 * 
 * 可选实现（5个）：
 *   - 符号链接操作：2个
 *   - 权限操作：1个
 *   - 内存映射：2个
 *   - 递归删除：1个
 *   - 文件时间：1个
 *   - 驱动器列表：1个
 */

#ifndef XFILESYSTEM_PLATFORM_H
#define XFILESYSTEM_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include "XFileInfo.h"
#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 目录迭代器
 * ============================================================================ */

typedef struct XDirEntry {
    char name[256];               /**< 文件名 */
    uint8_t isDir       : 1;      /**< 是否为目录 */
    uint8_t isFile      : 1;      /**< 是否为普通文件 */
    uint8_t isSymLink   : 1;      /**< 是否为符号链接 */
    uint8_t isHidden    : 1;      /**< 是否为隐藏文件 */
    uint8_t _reserved   : 4;      /**< 保留位 */
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
 * @param path 文件路径
 * @param mode 打开模式（XFile_OpenMode组合）
 * @param error 输出错误码（可为NULL）
 * @return 文件描述符，失败返回-1
 * @note 平台层需实现：Windows CreateFile, Linux open
 */
int XFileSystem_open(const char* path, int mode, int* error);

/**
 * @brief 关闭文件
 * @param fd 文件描述符
 * @note 平台层需实现：Windows CloseHandle, Linux close
 */
void XFileSystem_close(int fd);

/**
 * @brief 获取文件当前位置
 * @param fd 文件描述符
 * @return 当前位置，失败返回-1
 * @note 平台层需实现：Windows SetFilePointer, Linux lseek
 */
int64_t XFileSystem_pos(int fd);

/**
 * @brief 移动文件指针
 * @param fd 文件描述符
 * @param pos 目标位置
 * @return 成功返回true
 * @note 平台层需实现：Windows SetFilePointer, Linux lseek
 */
bool XFileSystem_seek(int fd, int64_t pos);

/**
 * @brief 读取文件数据
 * @param fd 文件描述符
 * @param buf 目标缓冲区
 * @param len 最大读取长度
 * @return 实际读取字节数，-1表示错误
 * @note 平台层需实现：Windows ReadFile, Linux read
 */
int64_t XFileSystem_read(int fd, void* buf, int64_t len);

/**
 * @brief 写入文件数据
 * @param fd 文件描述符
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @return 实际写入字节数，-1表示错误
 * @note 平台层需实现：Windows WriteFile, Linux write
 */
int64_t XFileSystem_write(int fd, const void* buf, int64_t len);

/**
 * @brief 刷新文件缓冲区
 * @param fd 文件描述符
 * @return 成功返回true
 * @note 平台层需实现：Windows FlushFileBuffers, Linux fsync
 */
bool XFileSystem_flush(int fd);

/**
 * @brief 调整文件大小
 * @param fd 文件描述符
 * @param size 新大小
 * @return 成功返回true
 * @note 平台层需实现：Windows SetEndOfFile, Linux ftruncate
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
 * @note 平台层需实现：Windows GetFileAttributesEx, Linux stat
 */
bool XFileSystem_stat(const char* path, XFileStat* stat);

/**
 * @brief 获取文件属性（通过句柄）
 * @param fd 文件描述符
 * @param stat 输出文件属性结构
 * @return 成功返回true
 * @note 平台层需实现：Windows GetFileInformationByHandle, Linux fstat
 */
bool XFileSystem_fstat(int fd, XFileStat* stat);

/* ============================================================================
 * 三、文件系统操作（4个）- 必需实现
 * ============================================================================ */

/**
 * @brief 检查文件是否存在
 * @param path 文件路径
 * @return 存在返回true
 * @note 平台层需实现：Windows GetFileAttributes, Linux access
 */
bool XFileSystem_exists(const char* path);

/**
 * @brief 删除文件
 * @param path 文件路径
 * @return 成功返回true
 * @note 平台层需实现：Windows DeleteFile, Linux unlink
 */
bool XFileSystem_remove(const char* path);

/**
 * @brief 重命名文件
 * @param oldPath 原路径
 * @param newPath 新路径
 * @return 成功返回true
 * @note 平台层需实现：Windows MoveFile, Linux rename
 */
bool XFileSystem_rename(const char* oldPath, const char* newPath);

/**
 * @brief 复制文件
 * @param srcPath 源路径
 * @param dstPath 目标路径
 * @return 成功返回true
 * @note 平台层需实现：Windows CopyFile, Linux sendfile/read+write
 */
bool XFileSystem_copy(const char* srcPath, const char* dstPath);

/* ============================================================================
 * 四、目录操作（5个）- 必需实现
 * ============================================================================ */

/**
 * @brief 创建目录
 * @param path 目录路径
 * @param recursive 是否递归创建父目录
 * @return 成功返回true
 * @note 平台层需实现：Windows CreateDirectory, Linux mkdir
 *       recursive=true时需递归创建所有父目录
 */
bool XFileSystem_mkdir(const char* path, bool recursive);

/**
 * @brief 删除空目录
 * @param path 目录路径
 * @return 成功返回true
 * @note 平台层需实现：Windows RemoveDirectory, Linux rmdir
 *       仅能删除空目录，非空目录需用 rmdir_recursive
 */
bool XFileSystem_rmdir(const char* path);

/**
 * @brief 打开目录迭代器
 * @param path 目录路径
 * @return 迭代器句柄，失败返回NULL
 * @note 平台层需实现：Windows FindFirstFile, Linux opendir
 */
XDirIterator XFileSystem_opendir(const char* path);

/**
 * @brief 读取下一个目录项
 * @param iter 迭代器句柄
 * @param entry 输出目录项信息
 * @return 成功返回true，枚举结束返回false
 * @note 平台层需实现：Windows FindNextFile, Linux readdir
 */
bool XFileSystem_readdir(XDirIterator iter, XDirEntry* entry);

/**
 * @brief 关闭目录迭代器
 * @param iter 迭代器句柄
 * @note 平台层需实现：Windows FindClose, Linux closedir
 */
void XFileSystem_closedir(XDirIterator iter);

/* ============================================================================
 * 五、路径操作（1个）- 必需实现
 * ============================================================================ */

/**
 * @brief 解析路径
 * @param path 原始路径
 * @param result 存储结果的缓冲区
 * @param size 缓冲区大小
 * @param style 解析风格（绝对路径或规范化路径）
 * @return 成功返回true
 * @note 平台层需实现：
 *       - Absolute: Windows GetFullPathName, Linux realpath(不解析符号链接)
 *       - Canonical: Windows GetFinalPathNameByHandle, Linux realpath
 */
bool XFileSystem_resolvePath(const char* path, char* result, int size, XPathStyle style);

/* ============================================================================
 * 六、特殊路径（5个）
 * ============================================================================ */

/**
 * @brief 获取当前工作目录
 * @param path 输出缓冲区
 * @param pathSize 缓冲区大小
 * @return 成功返回true
 * @note 平台层需实现：Windows GetCurrentDirectory, Linux getcwd
 */
bool XFileSystem_currentPath(char* path, int pathSize);

/**
 * @brief 设置当前工作目录
 * @param path 目标路径
 * @return 成功返回true
 * @note 平台层需实现：Windows SetCurrentDirectory, Linux chdir
 */
bool XFileSystem_setCurrentPath(const char* path);

/**
 * @brief 获取用户主目录
 * @param path 输出缓冲区
 * @param pathSize 缓冲区大小
 * @return 成功返回true
 * @note 平台层需实现：
 *       - Windows: SHGetFolderPath(CSIDL_PROFILE)
 *       - Linux: getenv("HOME") 或 getpwuid
 */
bool XFileSystem_homePath(char* path, int pathSize);

/**
 * @brief 获取根目录
 * @param path 输出缓冲区
 * @param pathSize 缓冲区大小
 * @return 成功返回true
 * @note 平台层需实现：
 *       - Windows: 返回系统盘根目录如 "C:\"
 *       - Linux: 返回 "/"
 */
bool XFileSystem_rootPath(char* path, int pathSize);

/**
 * @brief 获取临时目录
 * @param path 输出缓冲区
 * @param pathSize 缓冲区大小
 * @return 成功返回true
 * @note 平台层需实现：
 *       - Windows: GetTempPath 或 GetEnvironmentVariable("TEMP")
 *       - Linux: P_tmpdir 或 "/tmp"
 */
bool XFileSystem_tempPath(char* path, int pathSize);

/* ============================================================================
 * 七、符号链接操作（2个）- 可选
 * ============================================================================ */

/**
 * @brief 创建链接
 * @param targetPath 目标路径
 * @param linkPath 链接路径
 * @return 成功返回true
 * @note 平台层需实现：
 *       - Windows: CreateSymbolicLink (需管理员权限)
 *       - Linux: symlink
 *       不支持可返回false
 */
bool XFileSystem_link(const char* targetPath, const char* linkPath);

/**
 * @brief 读取符号链接目标
 * @param path 符号链接路径
 * @param target 输出目标路径缓冲区
 * @param targetSize 缓冲区大小
 * @return 成功返回true
 * @note 平台层需实现：
 *       - Windows: DeviceIoControl(FSCTL_GET_REPARSE_POINT)
 *       - Linux: readlink
 */
bool XFileSystem_readLink(const char* path, char* target, int targetSize);

/* ============================================================================
 * 八、权限操作（1个）- 可选
 * ============================================================================ */

/**
 * @brief 设置文件权限
 * @param path 文件路径
 * @param permissions 权限位掩码（XFilePermissions）
 * @return 成功返回true
 * @note 平台层需实现：
 *       - Windows: 使用ACL或SetFileAttributes
 *       - Linux: chmod
 *       Windows权限模型与Unix不同，需映射
 */
bool XFileSystem_setPermissions(const char* path, XFilePermissions permissions);

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
 * @note 平台层需实现：
 *       - Windows: CreateFileMapping + MapViewOfFile
 *       - Linux: mmap
 */
void* XFileSystem_map(int fd, int64_t offset, int64_t size, bool writable);

/**
 * @brief 取消内存映射
 * @param addr 映射地址
 * @param size 映射大小
 * @return 成功返回true
 * @note 平台层需实现：
 *       - Windows: UnmapViewOfFile
 *       - Linux: munmap
 */
bool XFileSystem_unmap(void* addr, int64_t size);

/* ============================================================================
 * 十、递归删除目录（1个）- 可选
 * ============================================================================ */

/**
 * @brief 递归删除目录及其内容
 * @param path 目录路径
 * @return 成功返回true
 * @note 可在平台层实现，也可在应用层用 opendir/readdir/remove/rmdir 实现
 *       平台层实现可能更高效（如Windows SHFileOperation）
 */
bool XFileSystem_rmdir_recursive(const char* path);

/* ============================================================================
 * 十一、文件时间修改（1个）- 可选
 * ============================================================================ */

/**
 * @brief 设置文件时间
 * @param fd 文件描述符
 * @param timeType 时间类型（访问时间/修改时间/创建时间）
 * @param timeValue 时间值（Unix时间戳，毫秒）
 * @return 成功返回true
 * @note 平台层需实现：
 *       - Windows: SetFileTime
 *       - Linux: futimens
 */
bool XFileSystem_setFileTime(int fd, XFileTime timeType, int64_t timeValue);

/* ============================================================================
 * 十二、驱动器列表（1个）- 可选
 * ============================================================================ */

/**
 * @brief 获取驱动器/挂载点列表
 * @param drives 用于存储结果的数组，每个元素为驱动器路径（如 "C:\" 或 "/"）
 * @param maxCount 数组最大容量
 * @return 实际驱动器数量
 * 
 * Windows: 返回 "A:\", "B:\", ... "Z:\"
 * Unix/Linux: 返回 "/"
 * 
 * 平台层需实现：
 * - Windows: GetLogicalDriveStrings
 * - Linux: 可返回单一 "/" 或解析 /proc/mounts
 */
int XFileSystem_drives(char drives[][16], int maxCount);

#ifdef __cplusplus
}
#endif

#endif // XFILESYSTEM_PLATFORM_H