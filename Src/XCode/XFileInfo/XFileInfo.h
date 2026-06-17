#ifndef XFILEINFO_H
#define XFILEINFO_H

/**
 * @file XFileInfo.h
 * @brief 文件信息类，提供与操作系统无关的文件系统入口信息 API
 * 
 * 移植自 Qt 6.8 QFileInfo 类，提供跨平台的文件信息获取功能。
 */

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XString.h"
#include "XDateTime.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef  XVector XFileInfoList;
/* ============================================================================
 * 枚举定义
 * ============================================================================ */

/**
 * @brief 文件权限标志
 */
typedef enum XFilePermission {
    XFile_ReadOwner   = 0x4000,  /**< 所有者可读 */
    XFile_WriteOwner  = 0x2000,  /**< 所有者可写 */
    XFile_ExeOwner    = 0x1000,  /**< 所有者可执行 */
    XFile_ReadUser    = 0x0400,  /**< 用户可读 */
    XFile_WriteUser   = 0x0200,  /**< 用户可写 */
    XFile_ExeUser     = 0x0100,  /**< 用户可执行 */
    XFile_ReadGroup   = 0x0040,  /**< 用户组可读 */
    XFile_WriteGroup  = 0x0020,  /**< 用户组可写 */
    XFile_ExeGroup    = 0x0010,  /**< 用户组可执行 */
    XFile_ReadOther   = 0x0004,  /**< 其他用户可读 */
    XFile_WriteOther  = 0x0002,  /**< 其他用户可写 */
    XFile_ExeOther    = 0x0001,  /**< 其他用户可执行 */
    
    /* 权限组合 */
    XFile_PermsOwner  = XFile_ReadOwner | XFile_WriteOwner | XFile_ExeOwner,
    XFile_PermsUser   = XFile_ReadUser  | XFile_WriteUser  | XFile_ExeUser,
    XFile_PermsGroup  = XFile_ReadGroup | XFile_WriteGroup | XFile_ExeGroup,
    XFile_PermsOther  = XFile_ReadOther | XFile_WriteOther | XFile_ExeOther,
    XFile_PermsAll    = XFile_PermsOwner | XFile_PermsGroup | XFile_PermsOther
} XFilePermission;

typedef int XFilePermissions;

/**
 * @brief 文件时间类型
 */
typedef enum XFileTime {
    XFile_AccessTime         = 0,  /**< 访问时间 */
    XFile_BirthTime          = 1,  /**< 创建时间 */
    XFile_MetadataChangeTime = 2,  /**< 元数据修改时间 */
    XFile_ModificationTime   = 3   /**< 内容修改时间 */
} XFileTime;

/* ============================================================================
 * 虚函数表定义
 * ============================================================================ */

XCLASS_DEFINE_BEGING(XFileInfo)
XCLASS_DEFINE_EXTEND_END(XFileInfo, XClass)

/* ============================================================================
 * 结构体定义
 * ============================================================================ */

/**
 * @brief XFileInfo 结构体，表示文件系统入口信息
 */
typedef struct XFileInfo {
    XClass m_class;               /**< 基类 */
    XString* m_filePath;          /**< 文件路径 */
    bool m_caching;               /**< 是否启用缓存 */
    bool m_cacheValid;            /**< 缓存是否有效 */
    
    /* 缓存的文件信息 */
    int64_t m_size;               /**< 文件大小 */
    int64_t m_birthTime;          /**< 创建时间 (Unix时间戳) */
    int64_t m_metadataChangeTime; /**< 元数据修改时间 */
    int64_t m_modificationTime;   /**< 内容修改时间 */
    int64_t m_accessTime;         /**< 访问时间 */
    XFilePermissions m_permissions; /**< 文件权限 */
    uint32_t m_ownerId;           /**< 所有者ID */
    uint32_t m_groupId;           /**< 用户组ID */
    bool m_exists;                /**< 是否存在 */
    bool m_isFile;                /**< 是否为文件 */
    bool m_isDir;                 /**< 是否为目录 */
    bool m_isSymLink;             /**< 是否为符号链接 */
    bool m_isShortcut;            /**< 是否为快捷方式(Windows) */
    bool m_isJunction;            /**< 是否为Junction(Windows) */
    bool m_isHidden;              /**< 是否为隐藏 */
    bool m_isReadable;            /**< 是否可读 */
    bool m_isWritable;            /**< 是否可写 */
    bool m_isExecutable;          /**< 是否可执行 */
} XFileInfo;

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

XVtable* XFileInfo_class_init(void);

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

#define XFileInfo_delete_base    XClass_delete_base
#define XFileInfo_deinit_base    XClass_deinit_base
#define XFileInfo_copy_base      XClass_copy_base
#define XFileInfo_move_base      XClass_move_base

/**
 * @brief 创建一个空的 XFileInfo 对象
 */
XFileInfo* XFileInfo_create_1(void);

/**
 * @brief 创建一个指定路径的 XFileInfo 对象
 * @param path 文件路径
 */
XFileInfo* XFileInfo_create_2(const XString* path);

/**
 * @brief 创建一个相对于目录的 XFileInfo 对象
 * @param dir 目录路径
 * @param path 相对文件路径
 */
XFileInfo* XFileInfo_create_3(const XString* dir, const XString* path);

/**
 * @brief 初始化空的 XFileInfo 对象
 */
void XFileInfo_init_1(XFileInfo* info);

/**
 * @brief 初始化指定路径的 XFileInfo 对象
 */
void XFileInfo_init_2(XFileInfo* info, const XString* path);

/**
 * @brief 初始化相对于目录的 XFileInfo 对象
 */
void XFileInfo_init_3(XFileInfo* info, const XString* dir, const XString* path);

/* ============================================================================
 * 文件路径
 * ============================================================================ */

/**
 * @brief 设置文件路径
 */
void XFileInfo_setFile_1(XFileInfo* info, const XString* path);

/**
 * @brief 设置相对于目录的文件路径
 */
void XFileInfo_setFile_2(XFileInfo* info, const XString* dir, const XString* path);

/**
 * @brief 获取文件路径（可能是相对或绝对路径）
 */
const XString* XFileInfo_filePath(const XFileInfo* info);

/**
 * @brief 获取绝对文件路径
 */
XString* XFileInfo_absoluteFilePath(const XFileInfo* info);

/**
 * @brief 获取规范文件路径（解析符号链接和 "." ".."）
 */
XString* XFileInfo_canonicalFilePath(const XFileInfo* info);

/**
 * @brief 获取文件名（不含路径）
 */
XString* XFileInfo_fileName(const XFileInfo* info);

/**
 * @brief 获取基本名称（第一个 '.' 之前的部分）
 */
XString* XFileInfo_baseName(const XFileInfo* info);

/**
 * @brief 获取完整基本名称（最后一个 '.' 之前的部分）
 */
XString* XFileInfo_completeBaseName(const XFileInfo* info);

/**
 * @brief 获取后缀（最后一个 '.' 之后的部分）
 */
XString* XFileInfo_suffix(const XFileInfo* info);

/**
 * @brief 获取完整后缀（第一个 '.' 之后的部分）
 */
XString* XFileInfo_completeSuffix(const XFileInfo* info);

/**
 * @brief 获取路径（不含文件名）
 */
XString* XFileInfo_path(const XFileInfo* info);

/**
 * @brief 获取绝对路径（不含文件名）
 */
XString* XFileInfo_absolutePath(const XFileInfo* info);

/**
 * @brief 获取规范路径（不含文件名）
 */
XString* XFileInfo_canonicalPath(const XFileInfo* info);

/* ============================================================================
 * 文件类型检查
 * ============================================================================ */

/**
 * @brief 检查文件是否存在
 */
bool XFileInfo_exists(const XFileInfo* info);

/**
 * @brief 静态方法：检查指定路径的文件是否存在
 */
bool XFileInfo_exists_static(const XString* path);

/**
 * @brief 刷新文件信息缓存
 */
void XFileInfo_refresh(XFileInfo* info);

/**
 * @brief 立即从文件系统读取所有属性
 */
void XFileInfo_stat(XFileInfo* info);

/**
 * @brief 检查是否为普通文件
 */
bool XFileInfo_isFile(const XFileInfo* info);

/**
 * @brief 检查是否为目录
 */
bool XFileInfo_isDir(const XFileInfo* info);

/**
 * @brief 检查是否为符号链接
 */
bool XFileInfo_isSymLink(const XFileInfo* info);

/**
 * @brief 检查是否为符号链接（不包括快捷方式和别名）
 */
bool XFileInfo_isSymbolicLink(const XFileInfo* info);

/**
 * @brief 检查是否为快捷方式（Windows .lnk 文件）
 */
bool XFileInfo_isShortcut(const XFileInfo* info);

/**
 * @brief 检查是否为 Junction（Windows NTFS）
 */
bool XFileInfo_isJunction(const XFileInfo* info);

/**
 * @brief 检查是否为根目录
 */
bool XFileInfo_isRoot(const XFileInfo* info);

/**
 * @brief 检查是否为 Bundle（macOS/iOS）
 */
bool XFileInfo_isBundle(const XFileInfo* info);

/**
 * @brief 检查是否为隐藏文件
 */
bool XFileInfo_isHidden(const XFileInfo* info);

/* ============================================================================
 * 路径类型检查
 * ============================================================================ */

/**
 * @brief 检查路径是否为绝对路径
 */
bool XFileInfo_isAbsolute(const XFileInfo* info);
bool XFileInfo_isAbsolutePath_static(const char* path);
/**
 * @brief 检查路径是否为相对路径
 */
bool XFileInfo_isRelative(const XFileInfo* info);

/**
 * @brief 将相对路径转换为绝对路径
 */
bool XFileInfo_makeAbsolute(XFileInfo* info);

/**
 * @brief 检查路径是否为本地路径
 */
bool XFileInfo_isNativePath(const XFileInfo* info);

/* ============================================================================
 * 权限检查
 * ============================================================================ */

/**
 * @brief 检查是否可读
 */
bool XFileInfo_isReadable(const XFileInfo* info);

/**
 * @brief 检查是否可写
 */
bool XFileInfo_isWritable(const XFileInfo* info);

/**
 * @brief 检查是否可执行
 */
bool XFileInfo_isExecutable(const XFileInfo* info);

/**
 * @brief 检查指定权限
 */
bool XFileInfo_permission(const XFileInfo* info, XFilePermissions permissions);

/**
 * @brief 获取所有权限
 */
XFilePermissions XFileInfo_permissions(const XFileInfo* info);

/* ============================================================================
 * 文件属性
 * ============================================================================ */

/**
 * @brief 获取文件大小（字节）
 */
int64_t XFileInfo_size(const XFileInfo* info);

/**
 * @brief 获取创建时间
 */
XDateTime XFileInfo_birthTime(const XFileInfo* info);

/**
 * @brief 获取元数据修改时间
 */
XDateTime XFileInfo_metadataChangeTime(const XFileInfo* info);

/**
 * @brief 获取最后修改时间
 */
XDateTime XFileInfo_lastModified(const XFileInfo* info);

/**
 * @brief 获取最后访问时间
 */
XDateTime XFileInfo_lastRead(const XFileInfo* info);

/**
 * @brief 获取指定类型的文件时间
 */
XDateTime XFileInfo_fileTime(const XFileInfo* info, XFileTime time);

/* ============================================================================
 * 所有者信息
 * ============================================================================ */

/**
 * @brief 获取所有者名称
 */
XString* XFileInfo_owner(const XFileInfo* info);

/**
 * @brief 获取所有者ID
 */
uint32_t XFileInfo_ownerId(const XFileInfo* info);

/**
 * @brief 获取用户组名称
 */
XString* XFileInfo_group(const XFileInfo* info);

/**
 * @brief 获取用户组ID
 */
uint32_t XFileInfo_groupId(const XFileInfo* info);

/* ============================================================================
 * 符号链接
 * ============================================================================ */

/**
 * @brief 获取符号链接目标
 */
XString* XFileInfo_symLinkTarget(const XFileInfo* info);

/**
 * @brief 读取符号链接原始路径
 */
XString* XFileInfo_readSymLink(const XFileInfo* info);

/**
 * @brief 获取 Junction 目标
 */
XString* XFileInfo_junctionTarget(const XFileInfo* info);

/* ============================================================================
 * 缓存控制
 * ============================================================================ */

/**
 * @brief 检查是否启用缓存
 */
bool XFileInfo_caching(const XFileInfo* info);

/**
 * @brief 设置是否启用缓存
 */
void XFileInfo_setCaching(XFileInfo* info, bool enable);

/* ============================================================================
 * 比较操作
 * ============================================================================ */

/**
 * @brief 比较两个 XFileInfo 是否指向同一文件
 */
bool XFileInfo_equals(const XFileInfo* lhs, const XFileInfo* rhs);

#ifdef __cplusplus
}
#endif

#endif // XFILEINFO_H