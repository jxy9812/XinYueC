// XSocketNotifier_p.h
#ifndef XSOCKETNOTIFIER_P_H
#define XSOCKETNOTIFIER_P_H

#include "XSocketNotifier.h"

// 平台私有数据（由 .c 文件定义）
typedef struct PlatformData PlatformData;

typedef struct XSocketNotifierPrivate 
{
    XSocketDescriptor socket;
    XSocketNotifierType type;
    bool enabled;
    XSocketNotifierError error;
    PlatformData* platform; // 平台实现细节
} XSocketNotifierPrivate;
// ✅ 必须在这里定义！这是整个实现的基础
typedef struct XSocketNotifier {
    XObject base;               // 继承 XObject
    XSocketNotifierPrivate* d_ptr; // PIMPL 指针
}XSocketNotifier;
// 平台接口（由 posix/win32.c 实现）
bool XSocketNotifier_platform_setSocket(XSocketNotifierPrivate* d, XSocketDescriptor socket, XSocketNotifierType type);
void XSocketNotifier_platform_close(XSocketNotifierPrivate* d);
bool XSocketNotifier_platform_isValid(const XSocketNotifierPrivate* d);
bool XSocketNotifier_platform_isEnabled(const XSocketNotifierPrivate* d);
void XSocketNotifier_platform_setEnabled(XSocketNotifierPrivate* d, bool enabled);

#endif // XSOCKETNOTIFIER_P_H