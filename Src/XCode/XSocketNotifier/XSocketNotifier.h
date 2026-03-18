// XSocketNotifier.h
#ifndef XSOCKETNOTIFIER_H
#define XSOCKETNOTIFIER_H

#include "XObject.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 跨平台套接字描述符（opaque handle）
 *
 * - 在 POSIX 上代表 int fd
 * - 在 Windows 上代表 HANDLE
 * - 不透明类型，禁止直接解引用或比较
 */
typedef struct XSocketDescriptorImpl* XSocketDescriptor;

/**
 * @brief 获取无效描述符
 */
XSocketDescriptor XSocketDescriptor_Invalid(void);

/**
 * @brief 判断描述符是否有效
 */
bool XSocketDescriptor_isValid(XSocketDescriptor sd);

/**
 * @brief 从整数创建描述符（POSIX: fd, Windows: reinterpret_cast<HANDLE>(intptr)）
 *
 * 注意：此函数存在是为了兼容 Qt 的 qintptr 接口，
 * 但强烈建议使用平台专用函数（见下文）。
 */
XSocketDescriptor XSocketDescriptor_fromIntptr(intptr_t value);

/**
 * @brief 转换为 intptr_t（用于日志或调试，禁止用于逻辑）
 */
intptr_t XSocketDescriptor_toIntptr(XSocketDescriptor sd);

/**
 * @brief Notifier 类型（与 Qt::QSocketNotifier::Type 一一对应）
 */
typedef enum {
    XSocketNotifier_Read = 1,
    XSocketNotifier_Write = 2,
    XSocketNotifier_Exception = 4
} XSocketNotifierType;

/**
 * @brief 错误码
 */
typedef enum {
    XSocketNotifier_NoError = 0,
    XSocketNotifier_InvalidDescriptor,
    XSocketNotifier_ResourceError,
    XSocketNotifier_UnknownError
} XSocketNotifierError;

/**
 * @brief XSocketNotifier 类（模拟 Qt::QSocketNotifier）
 */
typedef struct XSocketNotifier XSocketNotifier;

/**
 * @brief 类型系统初始化（内部使用）
 */
XVtable* XSocketNotifier_class_init(void);

/**
 * @brief 构造函数 1：仅指定类型（后续需调用 setSocket）
 */
XSocketNotifier* XSocketNotifier_createWithType(XSocketNotifierType type);

/**
 * @brief 构造函数 2：指定 socket 和类型
 */
XSocketNotifier* XSocketNotifier_createWithSocket(XSocketDescriptor socket, XSocketNotifierType type);

/**
 * @brief 析构
 */
void XSocketNotifier_delete(XSocketNotifier* notifier);

/**
 * @brief 设置要监控的 socket
 */
void XSocketNotifier_setSocket(XSocketNotifier* notifier, XSocketDescriptor socket);

/**
 * @brief 获取当前 socket
 */
XSocketDescriptor XSocketNotifier_socket(const XSocketNotifier* notifier);

/**
 * @brief 获取类型
 */
XSocketNotifierType XSocketNotifier_type(const XSocketNotifier* notifier);

/**
 * @brief 是否绑定了有效 socket
 */
bool XSocketNotifier_isValid(const XSocketNotifier* notifier);

/**
 * @brief 是否已启用
 */
bool XSocketNotifier_isEnabled(const XSocketNotifier* notifier);

/**
 * @brief 启用/禁用监听
 */
void XSocketNotifier_setEnabled(XSocketNotifier* notifier, bool enabled);

/**
 * @brief 获取错误
 */
XSocketNotifierError XSocketNotifier_error(const XSocketNotifier* notifier);

/**
 * @brief 清除错误
 */
void XSocketNotifier_clearError(XSocketNotifier* notifier);

/**
 * @brief activated 信号名称（用于 XObject_connect）
 *
 * 信号签名: void activated(XSocketDescriptor socket, XSocketNotifierType type)
 */
void* XSocketNotifier_activated_signal(XSocketNotifier* notifier, XSocketDescriptor socket, XSocketNotifierType type);

#ifdef __cplusplus
}
#endif

#endif // XSOCKETNOTIFIER_H