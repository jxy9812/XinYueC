#ifndef XSTORAGEINFO_H
#define XSTORAGEINFO_H
#include "XFileSystem_config.h"

/**
 * @file XStorageInfo.h
 * @brief 存储设备信息类，提供有关已挂载存储和驱动器的信息
 *
 * 移植自 Qt 6.8 QStorageInfo 类，提供跨平台的存储设备信息获取功能。
 *
 * 功能：
 * - 获取存储设备空间信息（总大小、可用空间、空闲空间）
 * - 获取存储设备属性（设备路径、文件系统类型、挂载点）
 * - 检查存储设备状态（是否只读、是否就绪、是否为根卷）
 * - 获取所有已挂载的存储设备列表
 */

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XString.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XFILE_ON
#if XSTORAGEINFO_ON

/* ============================================================================
 * 虚函数表定义
 * ============================================================================ */

XCLASS_DEFINE_BEGING(XStorageInfo)
XCLASS_DEFINE_EXTEND_END(XStorageInfo, XClass)

/* ============================================================================
 * 结构体定义
 * ============================================================================ */

/**
 * @brief 存储设备信息数据结构体
 * @note 用于平台层返回存储设备信息
 */
typedef struct XStorageInfoData {
    int64_t bytesTotal;         /**< 总容量（字节） */
    int64_t bytesFree;          /**< 空闲空间（字节） */
    int64_t bytesAvailable;     /**< 可用空间（字节） */
    int blockSize;              /**< 块大小（字节） */
    XString* device;            /**< 设备路径 */
    XString* fileSystemType;    /**< 文件系统类型 */
    XString* volumeName;        /**< 卷标名称 */
    XString* subvolume;         /**< 子卷名称 */
    uint8_t isValid      : 1;   /**< 是否有效 */
    uint8_t isReady      : 1;   /**< 是否就绪 */
    uint8_t isReadOnly   : 1;   /**< 是否只读 */
    uint8_t isRemovable  : 1;   /**< 是否可移动设备 */
    uint8_t isRoot       : 1;   /**< 是否为根卷 */
    uint8_t _reserved    : 3;   /**< 保留位 */
} XStorageInfoData;

/**
 * @brief XStorageInfo 结构体，表示存储设备信息
 */
typedef struct XStorageInfo {
    XClass m_class;               /**< 基类 */
    XString* m_rootPath;          /**< 挂载点路径 */
    XStorageInfoData m_data;      /**< 存储设备信息数据 */
    uint16_t m_cacheValid    : 1;  /**< 缓存是否有效 */
    uint16_t _reserved      : 15; /**< 保留位 */
} XStorageInfo;

/* ============================================================================
 * 类型定义
 * ============================================================================ */

typedef XVector XStorageInfoList;

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

XVtable* XStorageInfo_class_init(void);

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

#define XStorageInfo_delete_base    XClass_delete_base
#define XStorageInfo_deinit_base    XClass_deinit_base
#define XStorageInfo_copy_base      XClass_copy_base
#define XStorageInfo_move_base      XClass_move_base

/**
 * @brief 默认构造函数
 * @note 创建一个无效的 XStorageInfo 对象
 */
XStorageInfo* XStorageInfo_create(void);

/**
 * @brief 从路径构造
 * @param path 路径（文件或目录）
 * @return 新创建的对象，失败返回 NULL
 */
XStorageInfo* XStorageInfo_create_2(const XString* path);

/**
 * @brief 从C字符串路径构造
 * @param path 路径字符串（UTF-8编码）
 * @return 新创建的对象，失败返回 NULL
 */
XStorageInfo* XStorageInfo_create_3(const char* path);

/**
 * @brief 拷贝构造函数
 * @param other 源对象
 * @return 新创建的对象，失败返回 NULL
 */
XStorageInfo* XStorageInfo_create_copy(const XStorageInfo* other);

/**
 * @brief 初始化函数
 * @param info 对象指针
 */
void XStorageInfo_init(XStorageInfo* info);

/**
 * @brief 从路径初始化
 * @param info 对象指针
 * @param path 路径
 */
void XStorageInfo_init_2(XStorageInfo* info, const XString* path);

/**
 * @brief 从C字符串路径初始化
 * @param info 对象指针
 * @param path 路径字符串（UTF-8编码）
 */
void XStorageInfo_init_3(XStorageInfo* info, const char* path);

/* ============================================================================
 * 设置函数
 * ============================================================================ */

/**
 * @brief 设置路径
 * @param info 对象指针
 * @param path 路径（文件或目录）
 * @note 路径可以是文件系统挂载点、目录或文件
 */
void XStorageInfo_setPath(XStorageInfo* info, const XString* path);

/**
 * @brief 设置路径（C字符串版本）
 * @param info 对象指针
 * @param path 路径字符串（UTF-8编码）
 */
void XStorageInfo_setPath_2(XStorageInfo* info, const char* path);

/**
 * @brief 刷新缓存
 * @param info 对象指针
 * @note XStorageInfo 缓存存储信息以提高性能，调用此方法可更新缓存
 */
void XStorageInfo_refresh(XStorageInfo* info);

/**
 * @brief 交换两个对象
 * @param lhs 左操作数
 * @param rhs 右操作数
 */
void XStorageInfo_swap(XStorageInfo* lhs, XStorageInfo* rhs);

/* ============================================================================
 * 状态检查
 * ============================================================================ */

/**
 * @brief 检查是否有效
 * @param info 对象指针
 * @return 有效返回 true
 * @note 如果指定的根路径存在且正确挂载，则返回 true
 */
bool XStorageInfo_isValid(const XStorageInfo* info);

/**
 * @brief 检查是否就绪
 * @param info 对象指针
 * @return 就绪返回 true
 * @note 例如，如果 CD 卷未插入，则返回 false
 */
bool XStorageInfo_isReady(const XStorageInfo* info);

/**
 * @brief 检查是否只读
 * @param info 对象指针
 * @return 只读返回 true
 */
bool XStorageInfo_isReadOnly(const XStorageInfo* info);

/**
 * @brief 检查是否为根卷
 * @param info 对象指针
 * @return 是根卷返回 true
 * @note Unix 系统根卷是挂载在 '/' 的卷；Windows 是操作系统安装的卷
 */
bool XStorageInfo_isRoot(const XStorageInfo* info);

/* ============================================================================
 * 空间信息
 * ============================================================================ */

/**
 * @brief 获取总容量
 * @param info 对象指针
 * @return 总容量（字节），无效对象返回 -1
 */
int64_t XStorageInfo_bytesTotal(const XStorageInfo* info);

/**
 * @brief 获取空闲空间
 * @param info 对象指针
 * @return 空闲空间（字节），无效对象返回 -1
 * @note 如果文件系统有配额限制，此值可能大于 bytesAvailable()
 */
int64_t XStorageInfo_bytesFree(const XStorageInfo* info);

/**
 * @brief 获取可用空间
 * @param info 对象指针
 * @return 当前用户可用空间（字节），无效对象返回 -1
 * @note 对于 root 用户或管理员，此值可能等于 bytesFree()
 */
int64_t XStorageInfo_bytesAvailable(const XStorageInfo* info);

/**
 * @brief 获取块大小
 * @param info 对象指针
 * @return 最佳传输块大小（字节），无效对象返回 -1
 */
int XStorageInfo_blockSize(const XStorageInfo* info);

/* ============================================================================
 * 设备信息
 * ============================================================================ */

/**
 * @brief 获取挂载点路径
 * @param info 对象指针
 * @return 挂载点路径，失败返回 NULL
 * @note Windows 上返回卷盘符（如 "C:\"）
 */
XString* XStorageInfo_rootPath(const XStorageInfo* info);

/**
 * @brief 获取设备路径
 * @param info 对象指针
 * @return 设备路径，失败返回 NULL
 * @note Unix 上返回 /dev/sda0 等设备路径；Windows 返回 UNC 路径（卷 GUID）
 */
XString* XStorageInfo_device(const XStorageInfo* info);

/**
 * @brief 获取文件系统类型
 * @param info 对象指针
 * @return 文件系统类型字符串，失败返回 NULL
 * @note 例如："NTFS"、"ext4"、"FAT32"、"exFAT"
 */
XString* XStorageInfo_fileSystemType(const XStorageInfo* info);

/**
 * @brief 获取卷标名称
 * @param info 对象指针
 * @return 卷标名称，无标签返回空字符串
 */
XString* XStorageInfo_name(const XStorageInfo* info);

/**
 * @brief 获取显示名称
 * @param info 对象指针
 * @return 卷标名称（如有）或挂载点路径
 */
XString* XStorageInfo_displayName(const XStorageInfo* info);

/**
 * @brief 获取子卷名称
 * @param info 对象指针
 * @return 子卷名称，无子卷返回空字符串
 * @note 某些文件系统（如 Btrfs）支持在一个设备上创建多个子卷
 */
XString* XStorageInfo_subvolume(const XStorageInfo* info);

/* ============================================================================
 * 静态函数
 * ============================================================================ */

/**
 * @brief 获取根卷信息
 * @return 根卷信息对象
 * @note Unix 返回 '/' 挂载的卷；Windows 返回系统盘
 */
XStorageInfo* XStorageInfo_root(void);

/**
 * @brief 获取所有已挂载的存储设备列表
 * @return 存储设备列表
 * @note Windows 返回"我的电脑"中可见的驱动器；
 *       Unix 返回所有已挂载的文件系统（伪文件系统除外）
 */
XStorageInfoList* XStorageInfo_mountedVolumes(void);

/* ============================================================================
 * 比较操作
 * ============================================================================ */

/**
 * @brief 相等比较
 * @param lhs 左操作数
 * @param rhs 右操作数
 * @return 相等返回 true
 * @note 两个无效对象比较总是返回 true
 */
bool XStorageInfo_equals(const XStorageInfo* lhs, const XStorageInfo* rhs);

#endif // XSTORAGEINFO_ON
#endif /* XFILE_ON */
#ifdef __cplusplus
}
#endif

#endif // XSTORAGEINFO_H
