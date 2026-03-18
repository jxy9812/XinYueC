// XSocketNotifier_win32.c
#if defined(_WIN32)

#include "XSocketNotifier_p.h"
#include "XEventLoop.h"
#include "XMemory.h"
//#include <windows.h>
#include <winsock2.h>
// ✅ 定义 Windows 版本的实现结构体
typedef struct XSocketDescriptorImpl {
    void* handle; // 实际是 HANDLE，但避免包含 windows.h 到头文件
}XSocketDescriptorImpl;
struct PlatformData {
    WSAEVENT wsaEvent;
    SOCKET sock;
    bool registered;
};

XSocketDescriptor XSocketDescriptor_Invalid(void) 
{
    return NULL;
}

bool XSocketDescriptor_isValid(XSocketDescriptor sd)
{
    return sd != NULL;
}

XSocketDescriptor XSocketDescriptor_fromIntptr(intptr_t value) {
    if (value == (intptr_t)INVALID_HANDLE_VALUE || value == -1) return NULL;
    XSocketDescriptorImpl* impl = (XSocketDescriptorImpl*)XMemory_malloc(sizeof(XSocketDescriptorImpl));
    impl->handle = (HANDLE)value;
    return (XSocketDescriptor)impl;
}

intptr_t XSocketDescriptor_toIntptr(XSocketDescriptor sd) {
    if (!sd) return -1;
    return (intptr_t)((XSocketDescriptorImpl*)sd)->handle;
}

bool XSocketNotifier_platform_setSocket(XSocketNotifierPrivate* d, XSocketDescriptor socket, XSocketNotifierType type) {
    XSocketNotifier_platform_close(d);

    if (!XSocketDescriptor_isValid(socket)) {
        d->socket = socket;
        d->type = type;
        return true;
    }

    HANDLE h = (HANDLE)XSocketDescriptor_toIntptr(socket);
    if (GetFileType(h) == FILE_TYPE_CHAR) {
        // 串口/控制台：不支持 WSAEventSelect
        d->error = XSocketNotifier_ResourceError;
        return false;
    }

    // 假设是 SOCKET（来自 WSASocket 或 accept）
    SOCKET sock = (SOCKET)h;
    WSAEVENT event = WSACreateEvent();
    if (event == WSA_INVALID_EVENT) return false;

    long lNetworkEvents = 0;
    if (type & XSocketNotifier_Read)      lNetworkEvents |= FD_READ | FD_ACCEPT;
    if (type & XSocketNotifier_Write)     lNetworkEvents |= FD_WRITE | FD_CONNECT;
    if (type & XSocketNotifier_Exception) lNetworkEvents |= FD_CLOSE | FD_OOB;

    if (WSAEventSelect(sock, event, lNetworkEvents) == SOCKET_ERROR) {
        WSACloseEvent(event);
        return false;
    }

    PlatformData* pd = (PlatformData*)XMemory_calloc(1, sizeof(PlatformData));
    pd->wsaEvent = event;
    pd->sock = sock;
    pd->registered = true;
    d->platform = pd;
    d->socket = socket;
    d->enabled = true;
    return true;
}

void XSocketNotifier_platform_close(XSocketNotifierPrivate* d) {
    if (d->platform) {
        PlatformData* pd = (PlatformData*)d->platform;
        if (pd->registered) {
            WSAEventSelect(pd->sock, pd->wsaEvent, 0);
            WSACloseEvent(pd->wsaEvent);
        }
        XMemory_free(pd);
        d->platform = NULL;
    }
    d->enabled = false;
}

bool XSocketNotifier_platform_isValid(const XSocketNotifierPrivate* d) {
    return XSocketDescriptor_isValid(d->socket);
}

bool XSocketNotifier_platform_isEnabled(const XSocketNotifierPrivate* d) {
    return d->enabled;
}

void XSocketNotifier_platform_setEnabled(XSocketNotifierPrivate* d, bool enabled) {
    if (enabled == d->enabled) return;
    if (enabled) {
        XSocketNotifier_platform_setSocket(d, d->socket, d->type);
    }
    else {
        XSocketNotifier_platform_close(d);
    }
}

// 在 XEventLoop_poll 中调用
void XSocketNotifier_handleEvent(XSocketNotifierPrivate* d) {
    PlatformData* pd = (PlatformData*)d->platform;
    WSANETWORKEVENTS netEvents;
    if (WSAEnumNetworkEvents(pd->sock, pd->wsaEvent, &netEvents) == 0) {
        XSocketNotifierType type = 0;
        if (netEvents.lNetworkEvents & (FD_READ | FD_ACCEPT | FD_CLOSE)) type |= XSocketNotifier_Read;
        if (netEvents.lNetworkEvents & (FD_WRITE | FD_CONNECT))           type |= XSocketNotifier_Write;
        if (netEvents.lNetworkEvents & (FD_CLOSE | FD_OOB))               type |= XSocketNotifier_Exception;

        if (type) {
            XSocketNotifier* notifier = (XSocketNotifier*)((char*)d - offsetof(XSocketNotifier, d_ptr));
            XSocketNotifier_activated_signal(notifier, d->socket, type);
        }
    }
}

#endif // _WIN32