// XSocketNotifier_posix.c
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)

#include "XSocketNotifier_p.h"
#include "XEventLoop.h"
#include "XEpoll.h"
#include "XMemory.h"
#include "XThread.h"
#include <fcntl.h>
#include <unistd.h>
typedef struct XSocketDescriptorImpl {
    int fd; 
}XSocketDescriptorImpl;

typedef struct PlatformData {
    int registered_fd;
    bool registered;
}PlatformData;

XSocketDescriptor XSocketDescriptor_Invalid(void) { return NULL; }

bool XSocketDescriptor_isValid(XSocketDescriptor sd) {
    return sd != NULL;
}

XSocketDescriptor XSocketDescriptor_fromIntptr(intptr_t value) {
    if (value < 0) return NULL;
    XSocketDescriptorImpl* impl = (XSocketDescriptorImpl*)XMemory_malloc(sizeof(XSocketDescriptorImpl));
    impl->fd = (int)value;
    return (XSocketDescriptor)impl;
}

intptr_t XSocketDescriptor_toIntptr(XSocketDescriptor sd) {
    if (!sd) return -1;
    return (intptr_t)((XSocketDescriptorImpl*)sd)->fd;
}

bool XSocketNotifier_platform_setSocket(XSocketNotifierPrivate* d, XSocketDescriptor socket, XSocketNotifierType type) {
    XSerialPort_platform_close(d);

    if (!XSocketDescriptor_isValid(socket)) {
        d->socket = socket;
        d->type = type;
        return true;
    }

    int fd = (int)XSocketDescriptor_toIntptr(socket);
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
        return false;
    }

    XEventLoop* loop = XThread_getCurrentEventLoop();
    if (!loop || !loop->m_epoll) {
        d->error = XSocketNotifier_ResourceError;
        return false;
    }

    PlatformData* pd = (PlatformData*)XMemory_calloc(1, sizeof(PlatformData));
    XEpollEvent event = { 0 };
    event.fd = fd;
    event.data = d;
    if (type & XSocketNotifier_Read)      event.events |= XEPOLLIN;
    if (type & XSocketNotifier_Write)     event.events |= XEPOLLOUT;
    if (type & XSocketNotifier_Exception) event.events |= XEPOLLERR | XEPOLLHUP;

    if (XEpoll_ctl(loop->m_epoll, XEPOLL_CTL_ADD, fd, &event) == 0) {
        pd->registered_fd = fd;
        pd->registered = true;
        d->platform = pd;
        d->socket = socket;
        d->enabled = true;
        return true;
    }

    XMemory_free(pd);
    return false;
}

void XSerialPort_platform_close(XSocketNotifierPrivate* d) {
    if (d->platform) {
        PlatformData* pd = (PlatformData*)d->platform;
        if (pd->registered) {
            XEventLoop* loop = XThread_getCurrentEventLoop();
            if (loop && loop->m_epoll) {
                XEpoll_ctl(loop->m_epoll, XEPOLL_CTL_DEL, pd->registered_fd, NULL);
            }
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
        XSerialPort_platform_close(d);
    }
}

// 在 XEventLoop_poll 中调用
void XSocketNotifier_posix_handleEvent(XSocketNotifierPrivate* d, uint32_t events) {
    XSocketNotifierType type = 0;
    if (events & (XEPOLLIN | XEPOLLPRI)) type |= XSocketNotifier_Read;
    if (events & XEPOLLOUT)              type |= XSocketNotifier_Write;
    if (events & (XEPOLLERR | XEPOLLHUP)) type |= XSocketNotifier_Exception;

    XSocketNotifier* notifier = (XSocketNotifier*)((char*)d - offsetof(XSocketNotifier, d_ptr));
    platform_emitActivated(notifier, d->socket, type);
}

#endif // POSIX