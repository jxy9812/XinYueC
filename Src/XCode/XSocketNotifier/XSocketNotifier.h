// XSocketNotifier.h
#ifndef XSOCKETNOTIFIER_H
#define XSOCKETNOTIFIER_H

#include "XObject.h"
#include "XSocketDescriptor.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Notifier 类型（与 Qt::QSocketNotifier::Type 一一对应）
 */
typedef enum {
    XSocketNotifier_Read = 1,
    XSocketNotifier_Write = 2,
    XSocketNotifier_Exception = 4
} XSocketNotifierType;
XCLASS_DEFINE_BEGING(XSocketNotifier)
XCLASS_DEFINE_EXTEND_END(XSocketNotifier, XObject)
/**
 * @brief XSocketNotifier 类（模拟 Qt::QSocketNotifier）
 *
 * 注意：该结构体在头文件中完整定义，但用户不应直接访问成员。
 */
typedef struct XSocketNotifier {
    XObject base;                     // 继承 XObject
    XSocketDescriptor socket;         // 当前绑定的 socket
    XSocketNotifierType type;         // 监听类型
    bool enabled;                     // 是否启用
}XSocketNotifier;
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
#define  XSocketNotifier_delete XObject_deleteLater
#define  XSocketNotifier_deinit XObject_deinitLater

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
 * @brief activated 信号名称（用于 XObject_connect）
 *
 * 信号签名: void activated(XSocketDescriptor socket, XSocketNotifierType type)
 */
void* XSocketNotifier_activated_signal(XSocketNotifier* notifier, XSocketDescriptor socket, XSocketNotifierType type);

#ifdef __cplusplus
}
#endif

#endif // XSOCKETNOTIFIER_H