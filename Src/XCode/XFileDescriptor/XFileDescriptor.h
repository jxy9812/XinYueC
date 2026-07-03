/**
 * @file XFileDescriptor.h
 * @brief 通用文件描述符表 —— 统一管理文件、套接字、串口、定时器等 I/O 资源
 *
 * 基于 XFixedPool O(1) 无锁分配/释放，对标 Linux fd 表设计。
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
} XFdType;

/* ============================================================================
 * 文件描述符结构体
 * ============================================================================ */

typedef struct XFileDescriptor {
    void*    handle;            /**< 平台句柄 */
    void*    ctx;               /**< 所属 XObject* */
    uint16_t type  : 4;         /**< XFdType 枚举值 0-15 */
    uint16_t refCount : 12;     /**< 引用计数 0-4095 */
} XFileDescriptor;

/* ============================================================================
 * 公共 API
 * ============================================================================ */

/**
 * @brief 初始化全局 fd 表（程序启动时调用一次）
 */
void XFd_init(void);

/**
 * @brief 分配一个文件描述符，O(1) 位图扫描 + XFixedPool 无锁分配
 * @param type   资源类型
 * @param handle 平台句柄
 * @param ctx    所属 XObject
 * @return fd（>=0），失败返回 -1
 */
intptr_t XFd_alloc(XFdType type, void* handle, void* ctx);

/**
 * @brief 释放文件描述符，O(1) XFixedPool 归还
 */
void XFd_free(intptr_t fd);

/**
 * @brief 通过 fd 获取 XFileDescriptor*，O(1)
 */
XFileDescriptor* XFd_get(intptr_t fd);

/**
 * @brief 获取底层句柄
 */
void* XFd_handle(intptr_t fd);

/**
 * @brief 获取资源类型
 */
XFdType XFd_type(intptr_t fd);

/**
 * @brief 获取所属 XObject 上下文
 */
void* XFd_ctx(intptr_t fd);

#ifdef __cplusplus
}
#endif

#endif /* XFILEDESCRIPTOR_H */