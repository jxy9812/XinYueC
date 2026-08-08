/**
 * @file XFileSystem.h
 * @brief 跨平台文件系统后端的公共抽象接口。
 * @details 本文件位于 Src/XPlatform，定义 XFile、XDir、XFileInfo 和
 *          XStorageInfo 与平台文件系统后端之间的契约，对齐 Qt 的 QFile、
 *          QDir、QFileInfo 和 QStorageInfo 的基础能力。声明本身不得调用
 *          Windows、POSIX 或嵌入式文件系统 API；具体实现在 Drive 目录中。
 *          路径和输出字符串均使用 XString，其内部按 UTF-16 代码单元存储；
 *          平台实现负责在边界处转换为平台编码。
 *
 * ============================================================================
 * API 分类统计（共34个平台函数 + 1个内联便捷函数）：
 * ============================================================================
 *
 * 一、核心文件操作（8个）- 必需实现
 * 二、文件属性操作（2个）- 必需实现（+ XFileSystem_exists 内联便捷函数）
 * 三、文件系统操作（3个）- 必需实现
 * 四、目录操作（5个）- 必需实现（rmdir 合并了递归删除）
 * 五、路径操作（1个）- 必需实现
 * 六、特殊路径（2个）- 合并了5个路径函数为 getSpecialPath + setCurrentPath
 * 七、链接操作（3个）- 可选
 * 八、权限操作（1个）- 可选
 * 九、内存映射（2个）- 可选
 * 十、文件时间修改（1个）- 仅fd版，路径版由上层 open→setFileTime→close 组合
 * 十一、驱动器列表（2个）- 可选
 * 十二、存储设备信息（1个）- 可选
 * 十三、磁盘格式化（1个）- 可选
 */

#ifndef XFILESYSTEM_H
#define XFILESYSTEM_H

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

/**
 * @brief 目录迭代器返回的一项目录条目。
 * @details 调用 XFileSystem_readdir 前，调用者必须创建 name 指向的 XString。
 *          平台层只写入该 XString，不取得其所有权；其余字段由平台层覆盖写入。
 */
typedef struct XDirEntry {
    XString* name;            /**< 调用方拥有且已初始化的输出文件名 XString；不能为 NULL。 */
    uint8_t isDir       : 1;  /**< 条目是否为目录；0 或 1。 */
    uint8_t isFile      : 1;  /**< 条目是否为普通文件；0 或 1。 */
    uint8_t isSymLink   : 1;  /**< 条目是否为符号链接或等效重解析点；0 或 1。 */
    uint8_t isHidden    : 1;  /**< 条目是否为平台定义的隐藏文件；0 或 1。 */
    uint8_t _reserved   : 4;  /**< 保留位；调用方不得读取、修改或持久化。 */
} XDirEntry;

/** @brief 不透明目录迭代器句柄；NULL 表示创建失败或无效句柄。 */
typedef void* XDirIterator;

/* ============================================================================
 * 路径解析风格
 * ============================================================================ */

/** @brief 路径解析方式；枚举值不可按位组合。 */
typedef enum {
    XPathStyle_Absolute,    /**< 绝对路径；不要求解析符号链接。 */
    XPathStyle_Canonical    /**< 规范路径；解析符号链接且目标不存在时通常失败。 */
} XPathStyle;

/* ============================================================================
 * 特殊路径类型
 * ============================================================================ */

/** @brief 可由平台查询的特殊目录类型；枚举值不可按位组合。 */
typedef enum {
    XSpecialPath_Current,   /**< 进程当前工作目录。 */
    XSpecialPath_Home,      /**< 当前用户的主目录；嵌入式平台可能不支持。 */
    XSpecialPath_Root,      /**< 文件系统根目录。 */
    XSpecialPath_Temp,      /**< 临时目录；平台不支持时查询失败。 */
} XSpecialPath;

/* ============================================================================
 * 一、核心文件操作（8个）- 必需实现
 * ============================================================================ */

/**
 * @brief 按指定打开模式打开文件并返回统一描述符。
 * @param path 文件路径；借用，不能为 NULL。
 * @param mode 打开模式位组合，取值见 XFileSystem_config.h。
 * @param error 可选的调用方输出存储；成功时写入 0，失败时写入平台相关错误码。
 * @return 有效 XFd 表示成功；失败返回 XFD_INVALID，调用方不得关闭该无效值。
 * @note 返回的 XFd 由调用方拥有，必须使用 XFileSystem_close 关闭。
 */
XFd XFileSystem_open(const XString* path, int mode, int* error);

/**
 * @brief 打开平台标准输入并配置为非阻塞读取。
 * @param error 可选的调用方错误码存储；成功时写入 0，失败时写入平台错误码。
 * @return 成功返回由调用方拥有的 XFd；平台没有标准输入或操作失败返回 XFD_INVALID。
 * @note 返回描述符必须使用 XFileSystem_close 关闭；该 API 不改变进程标准输入的所有权。
 */
XFd XFileSystem_openStandardInput(int* error);

/**
 * @brief 从标准输入描述符执行一次非阻塞读取。
 * @param fd 由 XFileSystem_openStandardInput 返回的输入描述符。
 * @param buf 调用方提供的输出缓冲；len 非零时不能为 NULL。
 * @param len 本次最多读取的字节数；必须为非负值。
 * @return 正数表示读取字节数，0 表示当前没有数据，-1 表示输入已结束，
 *         -2 表示读取错误或描述符不是非阻塞标准输入。
 */
int64_t XFileSystem_readStandardInput(XFd fd, void* buf, int64_t len);

/**
 * @brief 设置标准输入终端是否回显用户输入字符。
 * @param fd 由 XFileSystem_openStandardInput 返回的标准输入描述符；借用。
 * @param enabled 为 true 时显示输入字符，为 false 时隐藏输入字符（适用于密码输入）。
 * @return 后端成功修改终端回显状态返回 true；描述符无效、输入不是终端或平台不支持返回 false。
 * @note 本函数只修改终端的回显标志，不关闭或重新打开描述符，也不改变非阻塞读取配置。
 *       管道、重定向文件和 FatFS 标准输入不具备终端回显能力，调用失败时保持原状态。
 *       调用方在隐藏密码后应在命令结束或发生错误时再次传入 true 恢复回显。
 */
bool XFileSystem_setStandardInputEcho(XFd fd, bool enabled);

/**
 * @brief 关闭文件描述符并释放其在 XFileDescriptor 表中的条目。
 * @param fd 由 XFileSystem_open 成功返回的文件描述符；无效描述符不执行操作。
 * @return 无。
 * @note 调用后 fd 失效，不能再次用于本接口。
 */
void XFileSystem_close(XFd fd);

/**
 * @brief 查询文件当前读写位置。
 * @param fd 已打开的文件描述符；借用。
 * @return 从文件起点起算的字节偏移；失败返回 -1。
 */
int64_t XFileSystem_pos(XFd fd);

/**
 * @brief 将文件当前读写位置移动到指定字节偏移。
 * @param fd 已打开的文件描述符；借用。
 * @param pos 从文件起点起算的目标字节偏移；必须非负。
 * @return 定位成功返回 true；描述符无效、偏移非法或平台调用失败返回 false。
 */
bool XFileSystem_seek(XFd fd, int64_t pos);

/**
 * @brief 从文件当前位置读取字节。
 * @param fd 已打开的文件描述符；借用。
 * @param buf 调用方提供的可写缓冲区；当 len 大于 0 时不能为 NULL。
 * @param len 请求读取的字节数；必须非负。
 * @return 实际读取字节数；到达文件尾可返回 0，发生错误返回 -1。
 */
int64_t XFileSystem_read(XFd fd, void* buf, int64_t len);

/**
 * @brief 向文件当前位置写入字节。
 * @param fd 已打开的文件描述符；借用。
 * @param buf 待读取的字节缓冲区；借用，当 len 大于 0 时不能为 NULL。
 * @param len 请求写入的字节数；必须非负。
 * @return 实际写入字节数；发生错误返回 -1，短写由调用方继续处理。
 */
int64_t XFileSystem_write(XFd fd, const void* buf, int64_t len);

/**
 * @brief 将已缓冲的文件数据提交到持久化介质。
 * @param fd 已打开的文件描述符；借用。
 * @return 平台确认提交成功返回 true；失败返回 false。
 */
bool XFileSystem_flush(XFd fd);

/**
 * @brief 调整文件逻辑大小。
 * @param fd 已打开的文件描述符；借用。
 * @param size 调整后的文件大小，单位为字节，必须非负。
 * @return 调整成功返回 true；失败返回 false。
 * @note 扩展后的新区域内容和截断后当前位置的具体语义由平台文件系统决定。
 */
bool XFileSystem_resize(XFd fd, int64_t size);

/* ============================================================================
 * 二、文件属性操作（2个）- 必需实现
 * ============================================================================ */

/**
 * @brief 查询路径指向条目的文件属性。
 * @param path 待查询路径；借用，不能为 NULL。
 * @param stat 调用方提供的 XFileStat 输出存储；不能为 NULL。
 * @return 查询成功返回 true；路径不存在或平台查询失败返回 false。
 * @note 仅当返回 true 时保证 stat 中的数据完整有效。
 */
bool XFileSystem_stat(const XString* path, XFileStat* stat);

/**
 * @brief 查询已打开文件描述符的文件属性。
 * @param fd 已打开的文件描述符；借用。
 * @param stat 调用方提供的 XFileStat 输出存储；不能为 NULL。
 * @return 查询成功返回 true；描述符无效或平台查询失败返回 false。
 * @note 仅当返回 true 时保证 stat 中的数据完整有效。
 */
bool XFileSystem_fstat(XFd fd, XFileStat* stat);

/**
 * @brief 检查路径是否存在。
 * @param path 待查询路径；借用，不能为 NULL。
 * @return 能成功查询且条目存在返回 true；不存在、参数无效或查询失败返回 false。
 * @note 这是 XFileSystem_stat 加 exists 判断的便捷内联函数，不区分不存在与权限错误。
 */
static inline bool XFileSystem_exists(const XString* path) {
    XFileStat st;
    return XFileSystem_stat(path, &st) && st.exists;
}

/* ============================================================================
 * 三、文件系统操作（3个）- 必需实现
 * ============================================================================ */

/**
 * @brief 删除路径指向的文件。
 * @param path 待删除路径；借用，不能为 NULL。
 * @return 删除成功返回 true；路径不存在、指向非空目录或平台调用失败返回 false。
 * @note 目录删除请使用 XFileSystem_rmdir；本函数不会转移 path 的所有权。
 */
bool XFileSystem_remove(const XString* path);

/**
 * @brief 将文件或目录重命名到新路径。
 * @param oldPath 原路径；借用，不能为 NULL。
 * @param newPath 新路径；借用，不能为 NULL。
 * @return 重命名成功返回 true；参数无效、目标已存在或平台调用失败返回 false。
 * @note 对齐 QFile::rename 的不覆盖目标语义；失败时不保证跨平台的原子性。
 */
bool XFileSystem_rename(const XString* oldPath, const XString* newPath);

/**
 * @brief 复制文件到尚不存在的目标路径。
 * @param srcPath 源文件路径；借用，不能为 NULL。
 * @param dstPath 目标文件路径；借用，不能为 NULL。
 * @return 完整复制成功返回 true；源文件无效、目标已存在或 I/O 失败返回 false。
 * @note 对齐 QFile::copy 的不覆盖目标语义；失败后目标路径是否留下部分文件取决于平台。
 */
bool XFileSystem_copy(const XString* srcPath, const XString* dstPath);

/* ============================================================================
 * 四、目录操作（5个）- 必需实现
 * ============================================================================ */

/**
 * @brief 创建目录。
 * @param path 待创建目录路径；借用，不能为 NULL。
 * @param recursive 为 true 时同时创建缺失的父目录；为 false 时只创建末级目录。
 * @return 目录已成功创建或已存在时返回 true；路径无效或平台调用失败返回 false。
 */
bool XFileSystem_mkdir(const XString* path, bool recursive);

/**
 * @brief 删除目录
 * @param path 待删除目录路径；借用，不能为 NULL。
 * @param recursive 为 true 时递归删除目录及内容；为 false 时只删除空目录。
 * @return 删除成功返回 true；路径不是目录、目录非空或平台调用失败返回 false。
 * @warning recursive 为 true 时会永久删除所有后代条目，调用前必须确认路径。
 */
bool XFileSystem_rmdir(const XString* path, bool recursive);

/**
 * @brief 打开目录并创建迭代器。
 * @param path 待遍历目录路径；借用，不能为 NULL。
 * @return 新建的目录迭代器；调用方必须使用 XFileSystem_closedir 释放，失败返回 NULL。
 */
XDirIterator XFileSystem_opendir(const XString* path);

/**
 * @brief 从目录迭代器读取下一个条目。
 * @param iter 由 XFileSystem_opendir 返回的有效迭代器；借用，不能为 NULL。
 * @param entry 调用方提供的条目输出存储；不能为 NULL，entry->name 必须为已初始化的 XString。
 * @return 成功读取一个条目返回 true；到达末尾或发生错误返回 false。
 * @note 仅当返回 true 时 entry 内容有效；调用方可通过额外平台错误信息区分末尾与失败。
 */
bool XFileSystem_readdir(XDirIterator iter, XDirEntry* entry);

/**
 * @brief 关闭目录迭代器并释放关联资源。
 * @param iter 由 XFileSystem_opendir 返回的迭代器；可为 NULL，此时不执行任何操作。
 * @return 无。
 * @note 调用后 iter 失效。
 */
void XFileSystem_closedir(XDirIterator iter);

/* ============================================================================
 * 五、路径操作（1个）- 必需实现
 * ============================================================================ */

/**
 * @brief 将路径解析为平台可用的绝对或规范路径。
 * @param path 待解析路径；借用，不能为 NULL。
 * @param result 调用方提供的已初始化 XString 输出对象；成功时写入解析结果，不能为 NULL。
 * @param style 解析方式；取值为 XPathStyle。
 * @return 解析成功返回 true；路径不存在、无法规范化或平台不支持返回 false。
 * @note XString 参数按 UTF-16 代码单元存储；不同平台对 Absolute 是否解析链接的能力可能不同。
 */
bool XFileSystem_resolvePath(const XString* path, XString* result, XPathStyle style);

/* ============================================================================
 * 六、特殊路径（2个）
 * ============================================================================ */

/**
 * @brief 获取特殊目录路径。
 * @param type 要查询的特殊路径类型；取值为 XSpecialPath。
 * @param path 调用方提供的已初始化 XString 输出对象；成功时写入路径，不能为 NULL。
 * @return 查询成功返回 true；当前平台不支持该路径类型或查询失败返回 false。
 */
bool XFileSystem_getSpecialPath(XSpecialPath type, XString* path);

/**
 * @brief 设置进程当前工作目录。
 * @param path 新的当前目录路径；借用，不能为 NULL。
 * @return 进程工作目录修改成功返回 true；路径无效、无权限或平台调用失败返回 false。
 * @warning 该操作影响整个进程的当前目录，多个线程同时调用时由调用方负责同步。
 */
bool XFileSystem_setCurrentPath(const XString* path);

/* ============================================================================
 * 七、链接操作（3个）- 可选
 * ============================================================================ */

/**
 * @brief 创建指向目标路径的符号链接。
 * @param targetPath 符号链接保存的目标路径；借用，不能为 NULL。
 * @param linkPath 要创建的链接路径；借用，不能为 NULL。
 * @return 创建成功返回 true；平台不支持、目标路径已存在或调用失败返回 false。
 */
bool XFileSystem_link(const XString* targetPath, const XString* linkPath);

/**
 * @brief 创建指向目标文件的硬链接。
 * @param targetPath 目标文件路径；借用，不能为 NULL。
 * @param hardLinkPath 要创建的硬链接路径；借用，不能为 NULL。
 * @return 创建成功返回 true；平台不支持、目标不是普通文件或调用失败返回 false。
 */
bool XFileSystem_hardLink(const XString* targetPath, const XString* hardLinkPath);

/**
 * @brief 读取符号链接中保存的目标路径。
 * @param path 符号链接路径；借用，不能为 NULL。
 * @param target 调用方提供的已初始化 XString 输出对象；成功时写入目标路径，不能为 NULL。
 * @return 读取成功返回 true；路径不是链接、平台不支持或调用失败返回 false。
 */
bool XFileSystem_readLink(const XString* path, XString* target);

/* ============================================================================
 * 八、权限操作（1个）- 可选
 * ============================================================================ */

/**
 * @brief 设置路径的访问权限。
 * @param path 待修改路径；借用，不能为 NULL。
 * @param permissions XFilePermissions 位组合；不支持的权限位由平台忽略或导致失败。
 * @return 平台接受并完成权限修改返回 true；路径无效、无权限或调用失败返回 false。
 */
bool XFileSystem_setPermissions(const XString* path, XFilePermissions permissions);

/* ============================================================================
 * 九、内存映射（2个）- 可选
 * ============================================================================ */

/**
 * @brief 将文件的指定区域映射到进程地址空间。
 * @param fd 已打开的文件描述符；借用。
 * @param offset 映射起始字节偏移；必须非负。
 * @param size 映射长度，单位为字节；必须大于 0。
 * @param flags XFileDeviceMemoryMapFlags 位组合；bit0 表示私有映射，bit1 表示请求可写映射。
 * @return 映射成功返回可访问的首字节地址；失败返回 NULL。
 * @note 返回地址由调用方持有，必须使用同一 size 调用 XFileSystem_unmap 解除映射。
 */
void* XFileSystem_map(XFd fd, int64_t offset, int64_t size, int flags);

/**
 * @brief 解除由 XFileSystem_map 创建的内存映射。
 * @param addr XFileSystem_map 成功返回的首字节地址；不能为 NULL。
 * @param size 与创建映射时相同的逻辑映射长度，单位为字节，必须大于 0。
 * @return 解除成功返回 true；参数无效或平台调用失败返回 false。
 * @note 调用成功后 addr 指向的内存不可再访问。
 */
bool XFileSystem_unmap(void* addr, int64_t size);

/* ============================================================================
 * 十、文件时间修改（1个）- 可选
 * ============================================================================ */

/**
 * @brief 通过文件描述符设置文件时间。
 * @param fd 已打开的文件描述符；借用。
 * @param timeType 要修改的时间类型，取值为 XFileTime。
 * @param timeValue 新的 Unix 时间戳，单位为秒。
 * @return 修改成功返回 true；描述符无效、时间类型不支持或平台调用失败返回 false。
 * @note 直接操作已打开的句柄。Win32 用 SetFileTime(HANDLE)，
 *       POSIX 用 futimens，FatFs 通过句柄内存储的路径调用 f_utime。
 *       路径版需求由上层通过 open→setFileTime→close 组合实现。
 */
bool XFileSystem_setFileTime(XFd fd, XFileTime timeType, int64_t timeValue);

/* ============================================================================
 * 十一、驱动器列表（2个）- 可选
 * ============================================================================ */

/**
 * @brief 查询当前平台可枚举的根路径数量。
 * @return 根路径数量；无法枚举时返回 0。
 * @note POSIX 通常返回一个根路径，Windows 可返回多个逻辑驱动器。
 */
int XFileSystem_drives_count(void);

/**
 * @brief 查询指定索引处的根路径。
 * @param index 根路径索引，范围为 0 到 XFileSystem_drives_count() - 1。
 * @param path 调用方提供的已初始化 XString 输出对象；成功时写入路径，不能为 NULL。
 * @return 索引有效且查询成功返回 true；索引越界或平台调用失败返回 false。
 */
bool XFileSystem_drives_at(int index, XString* path);

/* ============================================================================
 * 十二、存储设备信息（1个）- 可选
 * ============================================================================ */

/**
 * @brief 查询包含指定路径的存储设备信息。
 * @param path 待查询路径；借用，不能为 NULL。
 * @param info 调用方提供的 XStorageInfoData 输出存储；不能为 NULL，成功时由函数填充。
 * @return 查询成功返回 true；路径无效、设备不可用或平台调用失败返回 false。
 * @note 对齐 QStorageInfo 的基础容量和状态信息；仅当返回 true 时保证 info 内容有效。
 */
bool XFileSystem_getStorageInfo(const XString* path, XStorageInfoData* info);

/**
 * @brief 将文件移动到回收站（XDG Trash / Windows Shell / macOS Trash）
 * @param fileName 源文件路径；借用，不能为 NULL。
 * @param pathInTrash 可选的调用方输出 XString；非 NULL 时成功后写入回收站目标路径。
 * @return 成功返回 true，失败返回 false
 * @note POSIX 端按 FreeDesktop Trash 规范实现；Windows 端调用 SHFileOperationW；
 *       若平台不可用可退化为直接 unlink/DeleteFile，调用方不能据此假设一定可恢复。
 *       这与 XFileSystem_remove (XFile::remove) 的语义不同:
 *         remove     = 立即从文件系统中删除, 不可恢复
 *         moveToTrash = 移动到 OS 回收站, 用户可恢复
 */
bool XFileSystem_moveToTrash(const XString* fileName, XString* pathInTrash);

/* ============================================================================
 * 十三、磁盘格式化（1个）- 可选
 * ============================================================================ */

/** @brief 磁盘格式化选项；除 None 外的枚举值可以按位组合。 */
typedef enum XFileSystemFormatFlags {
    XFileSystemFormat_None       = 0,    /**< 不启用额外格式化选项。 */
    XFileSystemFormat_Quick      = 0x01, /**< 请求快速格式化。 */
    XFileSystemFormat_Force      = 0x02, /**< 忽略可由平台安全检查阻止的非致命条件。 */
    XFileSystemFormat_Compress   = 0x04, /**< 请求启用文件系统压缩；平台不支持时可能失败。 */
    XFileSystemFormat_Encrypt    = 0x08, /**< 请求启用文件系统加密；平台不支持时可能失败。 */
} XFileSystemFormatFlags;

/** @brief 目标文件系统类型；枚举值不可按位组合。 */
typedef enum XFileSystemType {
    XFileSystemType_Auto,  /**< 由平台选择默认文件系统类型。 */
    XFileSystemType_FAT32, /**< FAT32 文件系统。 */
    XFileSystemType_NTFS,  /**< NTFS 文件系统。 */
    XFileSystemType_exFAT, /**< exFAT 文件系统。 */
    XFileSystemType_EXT4,  /**< ext4 文件系统。 */
    XFileSystemType_F2FS,  /**< F2FS 文件系统。 */
} XFileSystemType;

/**
 * @brief 格式化进度回调。
 * @param progress 当前进度百分比，范围为 0 到 100。
 * @param userData 调用 XFileSystem_format 时传入的借用用户数据；可为 NULL。
 * @return 返回 true 继续格式化；返回 false 请求平台尽快取消。
 */
typedef bool (*XFileSystemFormatProgress)(int progress, void* userData);

/**
 * @brief 对指定存储设备执行格式化。
 * @param drive 待格式化的设备路径；借用，不能为 NULL。
 * @param fsType 目标文件系统类型；取值为 XFileSystemType。
 * @param volumeName 可选卷标；借用，可为 NULL。
 * @param flags XFileSystemFormatFlags 位组合。
 * @param clusterSize 请求的簇大小，单位为字节；0 表示由平台决定。
 * @param progress 可选进度回调；借用，可为 NULL。
 * @param userData 原样传给 progress 的用户数据；借用，可为 NULL。
 * @return 格式化完成返回 true；参数非法、被回调取消、平台不支持或命令失败返回 false。
 * @warning 此操作会销毁 drive 上的数据，调用者必须先完成权限、挂载状态和目标设备确认。
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

#endif /* XFILESYSTEM_H */
