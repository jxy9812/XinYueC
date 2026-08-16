/**
 * @file XFileDescriptor.h
 * @brief 通用文件描述符表 —— 统一管理文件、套接字、串口、定时器等 I/O 资源
 *
 * 基于 XFixedPool O(1) 无锁分配/释放，对标 Linux fd 表设计。
 * 新版本增加 XDevice 关联字段，供统一设备门面使用；
 * 旧 API（XFd_alloc/free/get/...）保持兼容，调用方无需修改。
 *
 * 使用示例：
 *   intptr_t fd = XFd_alloc(XFD_TYPE_FILE, filPtr, fileObj);
 *   XFileDescriptor* desc = XFd_get(fd);
 *   XFd_free(fd);
 */

#ifndef XFILEDESCRIPTOR_H
#define XFILEDESCRIPTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XTypes.h"

/* 前向声明：XDevice 定义在 Src/XDevice/XDevice.h */
typedef struct XDevice XDevice;

/* ============================================================================
 * 统一标识符类型
 * ============================================================================ */

/* ============================================================================
 * 配置
 * ============================================================================ */

#ifndef XFD_TABLE_SIZE
#define XFD_TABLE_SIZE  128     /**< fd 表容量，宏可覆盖 */
#endif

/* ============================================================================
 * 类型枚举（平台无关）
 * ============================================================================ */

typedef enum {
    XFD_TYPE_FREE = 0,          /**< 空闲槽位 */
    XFD_TYPE_FILE,              /**< 普通文件 */
    XFD_TYPE_SOCKET,            /**< 网络套接字（TCP/UDP） */
    XFD_TYPE_SERIAL,            /**< 串口设备 */
    XFD_TYPE_TIMER,             /**< 定时器 */
    XFD_TYPE_DIR,               /**< 目录迭代器 */
    XFD_TYPE_CONSOLE,           /**< 带平台私有输入状态的控制台 */
    XFD_TYPE_MAPPING,           /**< 共享内存映射段 */
    XFD_TYPE_CLASS,             /**< 外部注册设备类型（配合 XDevice 使用） */
} XFdType;

/* ============================================================================
 * 文件描述符结构体
 * ============================================================================ */

/**
 * @brief 统一文件/设备描述符。
 * @details 极简布局：设备引擎字段已全部移入 XDeviceContext（由 XDevice 管理，子类按需
 *          扩展），XFileDescriptor 只保留 3 个字段：
 *          - m_deviceCtx 统一保存设备打开上下文：XDevice 流程保存 Open 虚函数返回的 XDeviceContext*（借用）；旧子系统暂时用该槽保存各自后端对象/原生句柄，后续迁移；
 *          - object   所属对象/后端上下文（借用，可为 NULL）：IoRing 事件回调用 owner XObject*，共享内存等平台后端用平台私有上下文（其首个成员为 XObject）；
 *          - m_type 保存 XFdType 枚举值。
 *          该结构由 XFixedPool O(1) 管理。
 */
typedef struct XFileDescriptor {
    void*    m_deviceCtx;   /**< 统一设备打开上下文：XDevice 流程为 XDeviceContext*；旧子系统暂时为各自后端对象/原生句柄（借用以由 XDevice/后端管理）。 */
    void*    object;   /**< 所属 XObject* / 平台后端上下文（借用，可为 NULL）：IoRing 分发用 owner XObject*，共享内存等平台后端保存平台私有上下文（其首个成员为 XObject）。 */
    uint8_t  m_type;   /**< XFdType 枚举值。 */
} XFileDescriptor;

/* ============================================================================
 * 公共 API
 * ============================================================================ */

/**
 * @brief 初始化全局 fd 表（程序启动时调用一次）
 * @return 无
 */
void XFd_init(void);

/**
 * @brief 分配一个文件描述符，O(1) XFixedPool 无锁分配
 * @param type   资源类型
 * @param handle 统一设备/句柄（借用）：XDevice 流程为 XDeviceContext*，旧子系统为原生句柄或后端对象
 * @param object 统一句柄关联的所属对象/后端上下文（借用，可为 NULL）
 * @return fd（>=0），表满或参数非法返回 -1
 */
XFd XFd_alloc(XFdType type, void* handle, void* object);

/**
 * @brief 释放文件描述符，O(1) XFixedPool 归还
 * @param fd 文件描述符；重复释放被忽略
 * @return 无
 */
void XFd_free(XFd fd);

/**
 * @brief 通过 fd 获取 XFileDescriptor*，O(1)
 * @param fd 文件描述符
 * @return 有效描述符借用指针；fd 越界或槽位空闲返回 NULL
 */
XFileDescriptor* XFd_get(XFd fd);

/**
 * @brief 获取统一设备上下文/句柄
 * @param fd 文件描述符
 * @return 统一设备上下文借用指针（XDevice 流程为 XDeviceContext*，旧子系统为原生句柄/后端对象）；无效描述符返回 NULL
 */
void* XFd_handle(XFd fd);

/**
 * @brief 获取资源类型
 * @param fd 文件描述符
 * @return 资源类型；无效描述符返回 XFD_TYPE_FREE
 */
XFdType XFd_type(XFd fd);

/**
 * @brief 获取所属对象/后端上下文
 * @param fd 文件描述符
 * @return 所属对象/后端上下文借用指针；可为 NULL；无效描述符返回 NULL
 */
void* XFd_object(XFd fd);

/**
 * @brief 设置所属对象/后端上下文（供 IoRing 完成事件分发查找 owner 或平台后端保存私有上下文）
 * @param fd  文件描述符
 * @param object 所属对象/后端上下文（可为 NULL）
 * @return 无
 */
void XFd_setObject(XFd fd, void* object);

#ifdef __cplusplus
}
#endif

#endif /* XFILEDESCRIPTOR_H */
