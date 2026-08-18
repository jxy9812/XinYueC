/**
 * @file XDeviceFile.h
 * @brief 文件设备类及其平台实现接口。
 * @details XDeviceFile 是可扩展的文件设备类，同时承载 POSIX、Windows 和
 *          FatFs 平台所需的统一文件接口。平台源文件直接实现本头文件声明，
 *          通过 XFILE_USE_PLATFORM_API 与 XFILE_USE_FATFS 宏选择实现，不再额外
 *          引入独立的文件系统后端抽象层。路径和输出字符串均使用 XString，
 *          其内部按 UTF-16 代码单元存储；平台实现负责在边界处转换为平台编码。
 *
 * ============================================================================
 * API 分类统计（共33个平台函数 + 3个内联便捷函数）：
 * ============================================================================
 *
 * 一、核心文件操作（10个）- 必需实现（pos 合并进 seek）
 * 二、文件属性操作（2个）- 必需实现（+ XDeviceFile_exists 内联便捷函数）
 * 三、文件系统操作（3个）- 必需实现
 * 四、目录操作（5个）- 必需实现（rmdir 合并了递归删除）
 * 五、路径操作（1个）- 必需实现
 * 六、特殊路径（2个）- 合并了5个路径函数为 getSpecialPath + setCurrentPath
 * 七、链接操作（2个）- 可选（link 合并了符号/硬链接）
 * 八、权限操作（1个）- 可选
 * 九、内存映射（3个）- 可选
 * 十、文件时间修改（1个）- 仅fd版，路径版由上层 open→setFileTime→close 组合
 * 十一、驱动器列表（1个）- 可选（enumerateDrives 合并 count/at）
 * 十二、存储设备信息（1个）- 可选
 * 十三、磁盘格式化（1个）- 可选
 */

#ifndef XDEVICEFILE_H
#define XDEVICEFILE_H
#include "XFileSystem_config.h"

#include <stdint.h>
#include <stdbool.h>
#include "XDevice.h"
#include "XDateTime.h"
#include "XFileInfo.h"
#include "XStorageInfo.h"  /* XStorageInfoData定义 */

#ifdef __cplusplus
extern "C" {
#endif
#if XFILE_ON

/* 前向声明 */
struct XString;
typedef struct XString XString;

/** @brief XDeviceFile 类虚函数表；继承 XDevice，不新增虚槽。 */
XCLASS_DEFINE_BEGING(XDeviceFile)
XCLASS_DEFINE_EXTEND_END(XDeviceFile, XDevice)

/** @brief 文件设备类对象；基类必须是第一个成员。 */
typedef struct XDeviceFile
{
    XDevice m_base;
} XDeviceFile;

XVtable* XDeviceFile_class_init(void);
void XDeviceFile_init(XDeviceFile* self);
XDeviceFile* XDeviceFile_create(void);
bool XDeviceFile_register(void);

/* ============================================================================
 * 目录迭代器
 * ============================================================================ */

/**
 * @brief 目录迭代器返回的一项目录条目。
 * @details 调用 XDeviceFile_readdir 前，调用者必须创建 name 指向的 XString。
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
/**
 * @brief 打开平台标准输入并配置为非阻塞读取。
 * @param error 可选的调用方错误码存储；成功时写入 0，失败时写入平台错误码。
 * @return 成功返回由调用方拥有的 XFd；平台没有标准输入或操作失败返回 XFD_INVALID。
 * @note 返回描述符必须使用 XDeviceFile_close 关闭；该 API 不改变进程标准输入的所有权。
 */
XFd XDeviceFile_openStandardInput(int* error);

/* 标准输入读取直接使用 XDevice_read；终端回显使用 XDeviceFileCommand_SetStandardInputEcho。 */

/**
 * @brief 文件设备核心操作由 XDevice 基类统一提供。
 * @details XDeviceFile 不重复声明同签名的公共实现；具体文件设备通过重载
 *          EXDevice_Close/Read/Write/Seek/Flush/Resize 完成实际行为。
 */
#define XDeviceFile_close XDevice_close

/**
 * @brief 文件定位基准。
 * @note 枚举值不可按位组合。
 */
typedef enum {
    XSeekSet,  /**< 从文件起点起算。 */
    XSeekCur,  /**< 从当前读写位置起算。 */
    XSeekEnd,  /**< 从文件末尾起算。 */
} XSeekWhence;

/**
 * @brief 移动文件当前读写位置并返回新位置。
 * @param fd 已打开的文件描述符；借用。
 * @param offset 相对 whence 基准的字节偏移；SET 基准下必须非负。
 * @param whence 定位基准，取值为 XSeekWhence。
 * @return 成功返回新的字节偏移（从文件起点起算）；失败返回 -1。
 * @note 查询当前位置可使用内联便捷函数 XDeviceFile_pos。
 */
#define XDeviceFile_seek(fd, offset, whence) \
    XDevice_seek((fd), (offset), (XDeviceSeekWhence)(whence))

/**
 * @brief 查询文件当前读写位置（定位零偏移的便捷内联函数）。
 * @param fd 已打开的文件描述符；借用。
 * @return 从文件起点起算的字节偏移；失败返回 -1。
 */
static inline int64_t XDeviceFile_pos(XFd fd) {
    return XDevice_seek(fd, 0, XDeviceSeekWhence_Current);
}

/**
 * @brief 从文件当前位置读取字节。
 * @param fd 已打开的文件描述符；借用。
 * @param buf 调用方提供的可写缓冲区；当 len 大于 0 时不能为 NULL。
 * @param len 请求读取的字节数；必须非负。
 * @return 实际读取字节数；到达文件尾可返回 0，发生错误返回 -1。
 */
#define XDeviceFile_read XDevice_read

/**
 * @brief 向文件当前位置写入字节。
 * @param fd 已打开的文件描述符；借用。
 * @param buf 待读取的字节缓冲区；借用，当 len 大于 0 时不能为 NULL。
 * @param len 请求写入的字节数；必须非负。
 * @return 实际写入字节数；发生错误返回 -1，短写由调用方继续处理。
 */
#define XDeviceFile_write XDevice_write

/**
 * @brief 将已缓冲的文件数据提交到持久化介质。
 * @param fd 已打开的文件描述符；借用。
 * @return 平台确认提交成功返回 true；失败返回 false。
 */
#define XDeviceFile_flush XDevice_flush

/**
 * @brief 调整文件逻辑大小。
 * @param fd 已打开的文件描述符；借用。
 * @param size 调整后的文件大小，单位为字节，必须非负。
 * @return 调整成功返回 true；失败返回 false。
 * @note 扩展后的新区域内容和截断后当前位置的具体语义由平台文件系统决定。
 */
#define XDeviceFile_resize XDevice_resize

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
bool XDeviceFile_stat(const XString* path, XFileStat* stat);

/**
 * @brief 检查路径是否存在。
 * @param path 待查询路径；借用，不能为 NULL。
 * @return 能成功查询且条目存在返回 true；不存在、参数无效或查询失败返回 false。
 * @note 这是 XDeviceFile_stat 加 exists 判断的便捷内联函数，不区分不存在与权限错误。
 */
static inline bool XDeviceFile_exists(const XString* path) {
    XFileStat st;
    return XDeviceFile_stat(path, &st) && st.exists;
}

/* ============================================================================
 * 三、文件系统操作（3个）- 必需实现
 * ============================================================================ */

/**
 * @brief 删除方式。
 * @note 枚举值不可按位组合。
 */
typedef enum {
    XRemoveMode_Permanent,  /**< 立即从文件系统中删除，不可恢复。 */
    XRemoveMode_Trash,      /**< 移动到操作系统回收站，用户可恢复；平台不支持时退化为永久删除。 */
} XRemoveMode;

/**
 * @brief 按指定方式删除路径指向的文件或空目录。
 * @param path 待删除路径；借用，不能为 NULL。
 * @param mode 删除方式，取值为 XRemoveMode。
 * @param trashPath 可选的调用方输出 XString；mode 为 XRemoveMode_Trash
 *                  且非 NULL 时，成功后写入回收站目标路径（不可用时写空串）。
 * @return 删除成功返回 true；路径不存在、目录非空或平台调用失败返回 false。
 * @note 目录删除请使用 XDeviceFile_rmdir；本函数不会转移 path 的所有权。
 *       remove = 立即删除不可恢复；moveToTrash 等价于 XDeviceFile_remove(path,
 *       XRemoveMode_Trash, NULL)。
 */
bool XDeviceFile_remove(const XString* path, XRemoveMode mode, XString* trashPath);

/**
 * @brief 永久删除路径（便捷内联函数）。
 * @param path 待删除路径；借用，不能为 NULL。
 * @return 删除成功返回 true；失败返回 false。
 */
static inline bool XDeviceFile_removePermanent(const XString* path) {
    return XDeviceFile_remove(path, XRemoveMode_Permanent, NULL);
}

/**
 * @brief 将文件或目录重命名到新路径。
 * @param oldPath 原路径；借用，不能为 NULL。
 * @param newPath 新路径；借用，不能为 NULL。
 * @return 重命名成功返回 true；参数无效、目标已存在或平台调用失败返回 false。
 * @note 对齐 QFile::rename 的不覆盖目标语义；失败时不保证跨平台的原子性。
 */
bool XDeviceFile_rename(const XString* oldPath, const XString* newPath);

/**
 * @brief 复制文件到尚不存在的目标路径。
 * @param srcPath 源文件路径；借用，不能为 NULL。
 * @param dstPath 目标文件路径；借用，不能为 NULL。
 * @return 完整复制成功返回 true；源文件无效、目标已存在或 I/O 失败返回 false。
 * @note 对齐 QFile::copy 的不覆盖目标语义；失败后目标路径是否留下部分文件取决于平台。
 */
bool XDeviceFile_copy(const XString* srcPath, const XString* dstPath);

/* ============================================================================
 * 四、目录操作（5个）- 必需实现
 * ============================================================================ */

/**
 * @brief 创建目录。
 * @param path 待创建目录路径；借用，不能为 NULL。
 * @param recursive 为 true 时同时创建缺失的父目录；为 false 时只创建末级目录。
 * @return 目录已成功创建或已存在时返回 true；路径无效或平台调用失败返回 false。
 */
bool XDeviceFile_mkdir(const XString* path, bool recursive);

/**
 * @brief 删除目录
 * @param path 待删除目录路径；借用，不能为 NULL。
 * @param recursive 为 true 时递归删除目录及内容；为 false 时只删除空目录。
 * @return 删除成功返回 true；路径不是目录、目录非空或平台调用失败返回 false。
 * @warning recursive 为 true 时会永久删除所有后代条目，调用前必须确认路径。
 */
bool XDeviceFile_rmdir(const XString* path, bool recursive);

/**
 * @brief 打开目录并创建迭代器。
 * @param path 待遍历目录路径；借用，不能为 NULL。
 * @return 新建的目录迭代器；调用方必须使用 XDeviceFile_closedir 释放，失败返回 NULL。
 */
XDirIterator XDeviceFile_opendir(const XString* path);

/**
 * @brief 从目录迭代器读取下一个条目。
 * @param iter 由 XDeviceFile_opendir 返回的有效迭代器；借用，不能为 NULL。
 * @param entry 调用方提供的条目输出存储；不能为 NULL，entry->name 必须为已初始化的 XString。
 * @return 成功读取一个条目返回 true；到达末尾或发生错误返回 false。
 * @note 仅当返回 true 时 entry 内容有效；调用方可通过额外平台错误信息区分末尾与失败。
 */
bool XDeviceFile_readdir(XDirIterator iter, XDirEntry* entry);

/**
 * @brief 关闭目录迭代器并释放关联资源。
 * @param iter 由 XDeviceFile_opendir 返回的迭代器；可为 NULL，此时不执行任何操作。
 * @return 无。
 * @note 调用后 iter 失效。
 */
void XDeviceFile_closedir(XDirIterator iter);

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
bool XDeviceFile_resolvePath(const XString* path, XString* result, XPathStyle style);

/* ============================================================================
 * 六、特殊路径（2个）
 * ============================================================================ */

/**
 * @brief 获取特殊目录路径。
 * @param type 要查询的特殊路径类型；取值为 XSpecialPath。
 * @param path 调用方提供的已初始化 XString 输出对象；成功时写入路径，不能为 NULL。
 * @return 查询成功返回 true；当前平台不支持该路径类型或查询失败返回 false。
 */
bool XDeviceFile_getSpecialPath(XSpecialPath type, XString* path);

/**
 * @brief 设置进程当前工作目录。
 * @param path 新的当前目录路径；借用，不能为 NULL。
 * @return 进程工作目录修改成功返回 true；路径无效、无权限或平台调用失败返回 false。
 * @warning 该操作影响整个进程的当前目录，多个线程同时调用时由调用方负责同步。
 */
bool XDeviceFile_setCurrentPath(const XString* path);

/* ============================================================================
 * 七、链接操作（3个）- 可选
 * ============================================================================ */

/**
 * @brief 链接类型。
 * @note 枚举值不可按位组合。
 */
typedef enum {
    XLinkType_Symbolic, /**< 符号链接。 */
    XLinkType_Hard,     /**< 硬链接。 */
} XLinkType;

/**
 * @brief 创建指向目标路径的链接。
 * @param targetPath 链接保存的目标路径；借用，不能为 NULL。
 * @param linkPath 要创建的链接路径；借用，不能为 NULL。
 * @param type 链接类型，取值为 XLinkType。
 * @return 创建成功返回 true；平台不支持、目标路径已存在或调用失败返回 false。
 * @note 符号链接对应原 XDeviceFile_link，硬链接对应原 XDeviceFile_hardLink。
 */
bool XDeviceFile_link(const XString* targetPath, const XString* linkPath, XLinkType type);

/**
 * @brief 读取符号链接中保存的目标路径。
 * @param path 符号链接路径；借用，不能为 NULL。
 * @param target 调用方提供的已初始化 XString 输出对象；成功时写入目标路径，不能为 NULL。
 * @return 读取成功返回 true；路径不是链接、平台不支持或调用失败返回 false。
 */
bool XDeviceFile_readLink(const XString* path, XString* target);

/* ============================================================================
 * 八、权限操作（1个）- 可选
 * ============================================================================ */

/**
 * @brief 设置路径的访问权限。
 * @param path 待修改路径；借用，不能为 NULL。
 * @param permissions XFilePermissions 位组合；不支持的权限位由平台忽略或导致失败。
 * @return 平台接受并完成权限修改返回 true；路径无效、无权限或调用失败返回 false。
 */
bool XDeviceFile_setPermissions(const XString* path, XFilePermissions permissions);

/* ============================================================================
 * 九、内存映射（3个）- 可选
 * ============================================================================ */

/**
 * @brief 打开或创建命名共享内存段并返回统一描述符。
 * @param name 共享内存段名称；借用，不能为 NULL。
 * @param create 为 true 时创建新段（同名段已存在则直接等待对端连接）；
 *               为 false 时仅打开已存在的段，供进程间共享服务器创建的段。
 * @param maxSize 创建时的段大小（字节），必须大于 0；仅打开已有段时传 0。
 * @param error 可选的调用方错误码输出；成功时写入 0，失败时写入平台相关错误码。
 * @return 成功返回由调用方拥有的 XFd；失败返回 XFD_INVALID，调用方不得关闭该无效值。
 * @note 平台实现除命名共享内存段外，还会内建一个同名信令通道（POSIX 为
 *       Unix domain 流式套接字，Windows 为命名管道）。该 XFd 的
 *       XDevice_control 的 XDeviceFileCommand_Map / Unmap 用于访问共享内存数据区；
 *       XDeviceFile_read / XDeviceFile_write 在信令通道上收发 1 字节通知
 *       （数据方写完一块后写通知字节，对端阻塞等待，参考网络套接字的异步
 *       接收，不需要轮询共享内存状态字段）。使用完毕后必须调用
 *       XDeviceFile_close 释放。Windows 对应 CreateFileMapping/
 *       OpenFileMapping + 命名管道，POSIX 对应 shm_open + Unix domain
 *       套接字，嵌入式平台不支持时返回 XFD_INVALID。
 * @note create 为 true 时本调用会阻塞等待对端连接（与套接字 accept 语义
 *       一致），create 为 false 时阻塞直到服务端就绪（与 connect 语义一致）。
 */
XFd XDeviceFile_openSharedMemory(const XString* name, bool create, int64_t maxSize, int* error);

/* ============================================================================
 * 文件设备专有控制命令
 * ============================================================================ */

/** @brief 文件设备专有控制命令；命令编号继承自 XDeviceCommand。 */
typedef enum XDeviceFileCommand
{
    XDeviceFileCommand_GetFileStat = XDeviceCommand_Count
        /**< 查询已打开文件属性；in 为 NULL，out 为 XVarList(XFileStat stat)。 */,
    XDeviceFileCommand_Map
        /**< 映射文件区域；in 为 XVarList(int64_t offset, int64_t size, int flags)，out 为 XVarList(void* address)。 */,
    XDeviceFileCommand_Unmap
        /**< 解除文件映射；in 为 XVarList(void* address, int64_t size)，out 为 NULL。 */,
    XDeviceFileCommand_SetFileTime
        /**< 设置文件时间；in 为 XVarList(XFileTime timeType, int64_t timeValue)，out 为 NULL。 */,
    XDeviceFileCommand_SetStandardInputEcho
        /**< 设置标准输入终端回显；in 为 XVarList(bool enabled)，out 为 NULL。 */,
    XDeviceFileCommand_Count
        /**< 文件命令数量；文件子类从此值继续编号，不可传给 XDevice_control。 */
} XDeviceFileCommand;

/* ============================================================================
 * 十一、驱动器列表（1个）- 可选
 * ============================================================================ */

/** @brief 驱动器枚举回调。
 * @param path 当前根路径；借用在回调返回后失效。
 * @param userData 调用 XDeviceFile_enumerateDrives 时传入的借用用户数据；可为 NULL。
 * @return 返回 true 继续枚举；返回 false 请求平台尽快停止。
 */
typedef bool (*XDeviceFileDriveCallback)(const XString* path, void* userData);

/**
 * @brief 枚举当前平台可用的根路径。
 * @param callback 枚举回调；不能为 NULL。
 * @param userData 原样传给 callback 的用户数据；借用，可为 NULL。
 * @return 枚举正常结束返回 true；参数无效或回调请求停止返回 false。
 * @note POSIX 通常只枚举一个根路径 "/"，Windows 可枚举多个逻辑驱动器。
 */
bool XDeviceFile_enumerateDrives(XDeviceFileDriveCallback callback, void* userData);

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
bool XDeviceFile_getStorageInfo(const XString* path, XStorageInfoData* info);


/* ============================================================================
 * 十三、磁盘格式化（1个）- 可选
 * ============================================================================ */

/** @brief 磁盘格式化选项；除 None 外的枚举值可以按位组合。 */
typedef enum XDeviceFileFormatFlags {
    XFileSystemFormat_None       = 0,    /**< 不启用额外格式化选项。 */
    XFileSystemFormat_Quick      = 0x01, /**< 请求快速格式化。 */
    XFileSystemFormat_Force      = 0x02, /**< 忽略可由平台安全检查阻止的非致命条件。 */
    XFileSystemFormat_Compress   = 0x04, /**< 请求启用文件系统压缩；平台不支持时可能失败。 */
    XFileSystemFormat_Encrypt    = 0x08, /**< 请求启用文件系统加密；平台不支持时可能失败。 */
} XDeviceFileFormatFlags;

/** @brief 目标文件系统类型；枚举值不可按位组合。 */
typedef enum XDeviceFileType {
    XDeviceFileType_Auto,  /**< 由平台选择默认文件系统类型。 */
    XDeviceFileType_FAT32, /**< FAT32 文件系统。 */
    XDeviceFileType_NTFS,  /**< NTFS 文件系统。 */
    XDeviceFileType_exFAT, /**< exFAT 文件系统。 */
    XDeviceFileType_EXT4,  /**< ext4 文件系统。 */
    XDeviceFileType_F2FS,  /**< F2FS 文件系统。 */
} XDeviceFileType;

/**
 * @brief 格式化进度回调。
 * @param progress 当前进度百分比，范围为 0 到 100。
 * @param userData 调用 XDeviceFile_format 时传入的借用用户数据；可为 NULL。
 * @return 返回 true 继续格式化；返回 false 请求平台尽快取消。
 */
typedef bool (*XDeviceFileFormatProgress)(int progress, void* userData);

/**
 * @brief 对指定存储设备执行格式化。
 * @param drive 待格式化的设备路径；借用，不能为 NULL。
 * @param fsType 目标文件系统类型；取值为 XDeviceFileType。
 * @param volumeName 可选卷标；借用，可为 NULL。
 * @param flags XDeviceFileFormatFlags 位组合。
 * @param clusterSize 请求的簇大小，单位为字节；0 表示由平台决定。
 * @param progress 可选进度回调；借用，可为 NULL。
 * @param userData 原样传给 progress 的用户数据；借用，可为 NULL。
 * @return 格式化完成返回 true；参数非法、被回调取消、平台不支持或命令失败返回 false。
 * @warning 此操作会销毁 drive 上的数据，调用者必须先完成权限、挂载状态和目标设备确认。
 */
bool XDeviceFile_format(const XString* drive,
                        XDeviceFileType fsType,
                        const XString* volumeName,
                        int flags,
                        int clusterSize,
                        XDeviceFileFormatProgress progress,
                        void* userData);

#endif /* XFILE_ON */
#ifdef __cplusplus
}
#endif

#endif /* XDEVICEFILE_H */
