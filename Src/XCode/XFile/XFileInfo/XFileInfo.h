#ifndef XFILEINFO_H
#define XFILEINFO_H
#include "XFileSystem_config.h"

/**
 * @file XFileInfo.h
 * @brief 文件信息类，提供与操作系统无关的文件系统入口信息 API
 * 
 * 移植自 Qt 6.8 QFileInfo 类，提供跨平台的文件信息获取功能。
 * 
 * 设计说明：
 * - XFileInfo 包含 XFileStat 作为成员，避免字段重复定义
 * - XFileStat 由平台层填充，XFileInfo 提供缓存机制
 */

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XString.h"
#include "XDateTime.h"
#include "XIODevice.h"   /* XIODeviceBaseMode 枚举，XFileSystemOpenMode 通过宏映射到 XIODevice_* */

#ifdef __cplusplus
extern "C" {
#endif
#if XFILE_ON
#if XFILEINFO_ON
/**
 * @brief 文件权限标志
 */
typedef enum XFilePermission {
    XFile_ReadOwner = 0x4000,  /**< 所有者可读 */
    XFile_WriteOwner = 0x2000,  /**< 所有者可写 */
    XFile_ExeOwner = 0x1000,  /**< 所有者可执行 */
    XFile_ReadUser = 0x0400,  /**< 用户可读 */
    XFile_WriteUser = 0x0200,  /**< 用户可写 */
    XFile_ExeUser = 0x0100,  /**< 用户可执行 */
    XFile_ReadGroup = 0x0040,  /**< 用户组可读 */
    XFile_WriteGroup = 0x0020,  /**< 用户组可写 */
    XFile_ExeGroup = 0x0010,  /**< 用户组可执行 */
    XFile_ReadOther = 0x0004,  /**< 其他用户可读 */
    XFile_WriteOther = 0x0002,  /**< 其他用户可写 */
    XFile_ExeOther = 0x0001,  /**< 其他用户可执行 */

    /* 权限组合 */
    XFile_PermsOwner = XFile_ReadOwner | XFile_WriteOwner | XFile_ExeOwner,
    XFile_PermsUser = XFile_ReadUser | XFile_WriteUser | XFile_ExeUser,
    XFile_PermsGroup = XFile_ReadGroup | XFile_WriteGroup | XFile_ExeGroup,
    XFile_PermsOther = XFile_ReadOther | XFile_WriteOther | XFile_ExeOther,
    XFile_PermsAll = XFile_PermsOwner | XFile_PermsGroup | XFile_PermsOther
} XFilePermission;
typedef int XFilePermissions;
/* ============================================================================
 * 文件时间类型
 * ============================================================================ */

typedef enum XFileTime {
    XFile_AccessTime = 0,
    XFile_BirthTime = 1,
    XFile_MetadataChangeTime = 2,
    XFile_ModificationTime = 3
} XFileTime;
/* ============================================================================
 * 文件打开模式标志
 * ============================================================================ */

/* XFileSystemOpenMode 已统一到 XIODeviceBaseMode（位值完全对齐）
 * 以下宏保留向后兼容性，直接映射到 XIODevice_* */
#define XDeviceFile_ReadOnly   XIODevice_ReadOnly
#define XDeviceFile_WriteOnly  XIODevice_WriteOnly
#define XDeviceFile_ReadWrite  XIODevice_ReadWrite
#define XDeviceFile_Append     XIODevice_Append
#define XDeviceFile_Truncate   XIODevice_Truncate
#define XDeviceFile_Create     XIODevice_Create
#define XDeviceFile_NewOnly    XIODevice_NewOnly
#define XDeviceFile_Existing   XIODevice_Existing
#define XDeviceFile_Text       XIODevice_Text
/* ============================================================================
 * 文件属性结构体（纯数据结构，供平台层使用）
 *
 * 说明：此结构体被 XFileInfo 包含，避免字段重复定义
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
    XFilePermissions permissions;

    /* 所有者ID（平台相关，可选使用） */
    uint32_t ownerId;
    uint32_t groupId;

    /* 类型标志（使用位字段，10个标志位仅需4字节） */
    uint32_t exists : 1;  /**< 文件是否存在 */
    uint32_t isFile : 1;  /**< 是否为普通文件 */
    uint32_t isDir : 1;  /**< 是否为目录 */
    uint32_t isSymLink : 1;  /**< 是否为符号链接 */
    uint32_t isHidden : 1;  /**< 是否为隐藏文件 */
    uint32_t isReadable : 1;  /**< 是否可读 */
    uint32_t isWritable : 1;  /**< 是否可写 */
    uint32_t isExecutable : 1;  /**< 是否可执行 */
    uint32_t isJunction : 1;  /**< 是否为 Junction (Windows) */
    uint32_t isShortcut : 1;  /**< 是否为快捷方式 (Windows) */
    uint32_t _reserved : 22; /**< 保留位 */
} XFileStat;
typedef XVector XFileInfoList;

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
    
    /* 缓存控制（使用位字段节省内存） */
    uint16_t m_caching      : 1;  /**< 是否启用缓存 */
    uint16_t m_cacheValid   : 1;  /**< 缓存是否有效 */
    uint16_t _reserved      : 14; /**< 保留位 */
    
    /* 文件属性（直接嵌入 XFileStat，避免字段重复） */
    XFileStat m_stat;             /**< 文件属性数据 */
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

XFileInfo* XFileInfo_create_1(void);
XFileInfo* XFileInfo_create_2(const XString* path);
XFileInfo* XFileInfo_create_3(const XString* dir, const XString* path);

void XFileInfo_init_1(XFileInfo* info);
void XFileInfo_init_2(XFileInfo* info, const XString* path);
void XFileInfo_init_3(XFileInfo* info, const XString* dir, const XString* path);

/* ============================================================================
 * 文件路径
 * ============================================================================ */

void XFileInfo_setFile_1(XFileInfo* info, const XString* path);
void XFileInfo_setFile_2(XFileInfo* info, const XString* dir, const XString* path);

const XString* XFileInfo_filePath(const XFileInfo* info);
XString* XFileInfo_absoluteFilePath(const XFileInfo* info);
XString* XFileInfo_canonicalFilePath(const XFileInfo* info);
XString* XFileInfo_fileName(const XFileInfo* info);
XString* XFileInfo_baseName(const XFileInfo* info);
XString* XFileInfo_completeBaseName(const XFileInfo* info);
XString* XFileInfo_suffix(const XFileInfo* info);
XString* XFileInfo_completeSuffix(const XFileInfo* info);
XString* XFileInfo_path(const XFileInfo* info);
XString* XFileInfo_absolutePath(const XFileInfo* info);
XString* XFileInfo_canonicalPath(const XFileInfo* info);

/* ============================================================================
 * 文件类型检查
 * ============================================================================ */

bool XFileInfo_exists(const XFileInfo* info);
bool XFileInfo_exists_static(const XString* path);
void XFileInfo_refresh(XFileInfo* info);
void XFileInfo_stat(XFileInfo* info);
bool XFileInfo_isFile(const XFileInfo* info);
bool XFileInfo_isDir(const XFileInfo* info);
bool XFileInfo_isSymLink(const XFileInfo* info);
bool XFileInfo_isSymbolicLink(const XFileInfo* info);
bool XFileInfo_isShortcut(const XFileInfo* info);
bool XFileInfo_isJunction(const XFileInfo* info);
bool XFileInfo_isRoot(const XFileInfo* info);
bool XFileInfo_isBundle(const XFileInfo* info);
bool XFileInfo_isHidden(const XFileInfo* info);

/* ============================================================================
 * 路径类型检查
 * ============================================================================ */

bool XFileInfo_isAbsolute(const XFileInfo* info);
bool XFileInfo_isAbsolutePath_static(const XString* path);
bool XFileInfo_isRelative(const XFileInfo* info);
bool XFileInfo_makeAbsolute(XFileInfo* info);
bool XFileInfo_isNativePath(const XFileInfo* info);

/* ============================================================================
 * 权限检查
 * ============================================================================ */

bool XFileInfo_isReadable(const XFileInfo* info);
bool XFileInfo_isWritable(const XFileInfo* info);
bool XFileInfo_isExecutable(const XFileInfo* info);
bool XFileInfo_permission(const XFileInfo* info, XFilePermissions permissions);
XFilePermissions XFileInfo_permissions(const XFileInfo* info);

/* ============================================================================
 * 文件属性
 * ============================================================================ */

int64_t XFileInfo_size(const XFileInfo* info);
XDateTime XFileInfo_birthTime(const XFileInfo* info);
XDateTime XFileInfo_metadataChangeTime(const XFileInfo* info);
XDateTime XFileInfo_lastModified(const XFileInfo* info);
XDateTime XFileInfo_lastRead(const XFileInfo* info);
XDateTime XFileInfo_fileTime(const XFileInfo* info, XFileTime time);

/* ============================================================================
 * 所有者信息
 * ============================================================================ */

XString* XFileInfo_owner(const XFileInfo* info);
uint32_t XFileInfo_ownerId(const XFileInfo* info);
XString* XFileInfo_group(const XFileInfo* info);
uint32_t XFileInfo_groupId(const XFileInfo* info);

/* ============================================================================
 * 符号链接
 * ============================================================================ */

XString* XFileInfo_symLinkTarget(const XFileInfo* info);
XString* XFileInfo_readSymLink(const XFileInfo* info);
XString* XFileInfo_junctionTarget(const XFileInfo* info);

/* ============================================================================
 * 缓存控制
 * ============================================================================ */

bool XFileInfo_caching(const XFileInfo* info);
void XFileInfo_setCaching(XFileInfo* info, bool enable);

/* ============================================================================
 * 比较操作
 * ============================================================================ */

bool XFileInfo_equals(const XFileInfo* lhs, const XFileInfo* rhs);

#endif // XFILEINFO_ON
#endif /* XFILE_ON */
#ifdef __cplusplus
}
#endif

#endif // XFILEINFO_H
