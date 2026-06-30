#ifndef XFILEDEVICE_H
#define XFILEDEVICE_H

/**
 * @file XFileDevice.h
 * @brief 文件设备基类，提供文件读写的基础接口
 * 
 * 移植自 Qt 6.8 QFileDevice 类，继承自 XIODevice。
 * 为 QFile 和 XSaveFile 提供共享的文件操作功能。
 */

#include <stdint.h>
#include <stdbool.h>
#include "XIODevice.h"
#include "XString.h"
#include "XDateTime.h"
#include "XFileInfo.h"  // 引用 XFileInfo 中的 XFileTime, XFilePermission 等枚举

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 枚举定义（XFileDevice 特有）
 * ============================================================================ */

/**
 * @brief 文件错误类型
 * 
 * 描述文件操作可能返回的错误
 */
typedef enum XFileDeviceError {
    XFileDevice_NoError          = 0,   /**< 无错误 */
    XFileDevice_ReadError        = 1,   /**< 读取错误 */
    XFileDevice_WriteError       = 2,   /**< 写入错误 */
    XFileDevice_FatalError       = 3,   /**< 致命错误 */
    XFileDevice_ResourceError    = 4,   /**< 资源不足 */
    XFileDevice_OpenError        = 5,   /**< 打开失败 */
    XFileDevice_AbortError       = 6,   /**< 操作中止 */
    XFileDevice_TimeOutError     = 7,   /**< 超时 */
    XFileDevice_UnspecifiedError = 8,   /**< 未指定错误 */
    XFileDevice_RemoveError      = 9,   /**< 删除失败 */
    XFileDevice_RenameError      = 10,  /**< 重命名失败 */
    XFileDevice_PositionError    = 11,  /**< 定位失败 */
    XFileDevice_ResizeError      = 12,  /**< 调整大小失败 */
    XFileDevice_PermissionsError = 13,  /**< 权限错误 */
    XFileDevice_CopyError        = 14   /**< 复制失败 */
} XFileDeviceError;

/**
 * @brief 文件句柄标志
 */
typedef enum XFileDeviceFileHandleFlag {
    XFileDevice_AutoCloseHandle = 0x0001,  /**< 关闭时自动关闭句柄 */
    XFileDevice_DontCloseHandle = 0x0000   /**< 不关闭句柄 */
} XFileDeviceFileHandleFlag;

typedef int XFileDeviceFileHandleFlags;

/**
 * @brief 内存映射标志
 */
typedef enum XFileDeviceMemoryMapFlag {
    XFileDevice_NoOptions        = 0x0000,  /**< 无选项 */
    XFileDevice_MapPrivateOption = 0x0001   /**< 私有映射（修改不写回文件） */
} XFileDeviceMemoryMapFlag;

typedef int XFileDeviceMemoryMapFlags;

/* ============================================================================
 * 使用 XFileInfo 中的共享枚举
 * 
 * 以下枚举在 XFileInfo.h 中定义，XFileDevice 直接使用：
 * - XFileTime: 文件时间类型
 * - XFilePermission / XFilePermissions: 文件权限
 * ============================================================================ */

/* ============================================================================
 * 虚函数表定义
 * ============================================================================ */

#define XFILEDEVICE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XFileDevice))

XCLASS_DEFINE_BEGING(XFileDevice)
XCLASS_DEFINE_ENUM(XFileDevice, FileName) = XCLASS_VTABLE_GET_SIZE(XIODevice),
XCLASS_DEFINE_ENUM(XFileDevice, Resize),
XCLASS_DEFINE_ENUM(XFileDevice, Permissions),
XCLASS_DEFINE_ENUM(XFileDevice, SetPermissions),
XCLASS_DEFINE_END(XFileDevice)

/* ============================================================================
 * 结构体定义
 * ============================================================================ */

/**
 * @brief XFileDevice 结构体，文件设备基类
 */
typedef struct XFileDevice {
    XIODevice m_parent;               /**< 基类 XIODevice */
    
    /* 文件设备特有成员 */
    XFileDeviceError m_error;         /**< 最后的错误码 */
    intptr_t m_fileHandle;            /**< 文件句柄（-1 表示无效） */
    XFileDeviceFileHandleFlags m_handleFlags; /**< 句柄标志 */
    int64_t m_cachedSize;             /**< 缓存的文件大小 */
} XFileDevice;

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

/**
 * @brief 初始化 XFileDevice 类的虚函数表
 * @return 虚函数表指针
 */
XVtable* XFileDevice_class_init(void);

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

/**
 * @brief 创建 XFileDevice 对象
 * @return XFileDevice 对象指针，失败返回 NULL
 */
XFileDevice* XFileDevice_create(void);

/**
 * @brief 初始化 XFileDevice 对象
 * @param device XFileDevice 对象指针
 */
void XFileDevice_init(XFileDevice* device);

/* ============================================================================
 * 继承自 XIODevice 的虚函数（使用宏映射）
 * ============================================================================ */

#define XFileDevice_open_base             XIODevice_open_base
#define XFileDevice_close_base            XIODevice_close_base
#define XFileDevice_isSequential_base     XIODevice_isSequential_base
#define XFileDevice_pos_base              XIODevice_pos_base
#define XFileDevice_seek_base             XIODevice_seek_base
#define XFileDevice_atEnd_base            XIODevice_atEnd_base
#define XFileDevice_reset_base            XIODevice_reset_base
#define XFileDevice_size_base             XIODevice_size_base
#define XFileDevice_bytesAvailable_base   XIODevice_bytesAvailable_base
#define XFileDevice_bytesToWrite_base     XIODevice_bytesToWrite_base
#define XFileDevice_readData_base         XIODevice_readData_base
#define XFileDevice_writeData_base        XIODevice_writeData_base
#define XFileDevice_readLineData_base     XIODevice_readLineData_base
#define XFileDevice_skipData_base         XIODevice_skipData_base

/* ============================================================================
 * 析构函数（继承自 XObject）
 * ============================================================================ */

#define XFileDevice_deinitLater           XIODevice_deinitLater
#define XFileDevice_deleteLater           XIODevice_deleteLater

/* ============================================================================
 * XFileDevice 特有虚函数
 * ============================================================================ */

/**
 * @brief 虚函数：获取文件名
 * @param device XFileDevice 对象指针
 * @return 文件名（内部数据，不要释放），默认返回空字符串
 */
const XString* XFileDevice_fileName_base(const XFileDevice* device);

/**
 * @brief 虚函数：调整文件大小
 * @param device XFileDevice 对象指针
 * @param sz 新大小（字节）
 * @return 成功返回 true，失败返回 false
 */
bool XFileDevice_resize_base(XFileDevice* device, int64_t sz);

/**
 * @brief 虚函数：获取文件权限
 * @param device XFileDevice 对象指针
 * @return 权限标志组合
 */
XFilePermissions XFileDevice_permissions_base(const XFileDevice* device);

/**
 * @brief 虚函数：设置文件权限
 * @param device XFileDevice 对象指针
 * @param permissions 权限标志组合
 * @return 成功返回 true，失败返回 false
 */
bool XFileDevice_setPermissions_base(XFileDevice* device, XFilePermissions permissions);

/* ============================================================================
 * 错误处理（非虚函数）
 * ============================================================================ */

/**
 * @brief 获取最后的错误码
 * @param device XFileDevice 对象指针
 * @return 错误码
 */
XFileDeviceError XFileDevice_error(const XFileDevice* device);

/**
 * @brief 清除错误状态
 * @param device XFileDevice 对象指针
 */
void XFileDevice_unsetError(XFileDevice* device);

/* ============================================================================
 * 文件特有操作（非虚函数）
 * ============================================================================ */

/**
 * @brief 刷新缓冲区
 * @param device XFileDevice 对象指针
 * @return 成功返回 true，失败返回 false
 */
bool XFileDevice_flush(XFileDevice* device);

/**
 * @brief 获取文件句柄
 * @param device XFileDevice 对象指针
 * @return 文件句柄，失败返回 -1
 */
intptr_t XFileDevice_handle(const XFileDevice* device);

/* ============================================================================
 * 文件时间操作（非虚函数，平台相关实现）
 * ============================================================================ */

/**
 * @brief 获取文件时间
 * @param device XFileDevice 对象指针
 * @param time 时间类型（XFileTime）
 * @return 时间值，失败返回无效时间
 */
XDateTime XFileDevice_fileTime(const XFileDevice* device, XFileTime time);

/**
 * @brief 设置文件时间
 * @param device XFileDevice 对象指针
 * @param newDate 新时间值
 * @param time 时间类型（XFileTime）
 * @return 成功返回 true，失败返回 false
 * @note 文件必须已打开
 */
bool XFileDevice_setFileTime(XFileDevice* device, const XDateTime* newDate, XFileTime time);

/* ============================================================================
 * 内存映射（非虚函数，平台相关实现）
 * ============================================================================ */

/**
 * @brief 将文件映射到内存
 * @param device XFileDevice 对象指针
 * @param offset 起始偏移量
 * @param size 映射大小
 * @param flags 映射选项
 * @return 映射内存指针，失败返回 NULL
 */
void* XFileDevice_map(XFileDevice* device, int64_t offset, int64_t size, XFileDeviceMemoryMapFlags flags);

/**
 * @brief 取消内存映射
 * @param device XFileDevice 对象指针
 * @param address 映射内存地址
 * @return 成功返回 true，失败返回 false
 */
bool XFileDevice_unmap(XFileDevice* device, void* address);

#ifdef __cplusplus
}
#endif

#endif // XFILEDEVICE_H