// XSocketNotifier.c
#include "XSocketNotifier_p.h"
#include "XMemory.h"
#include "XVariant.h"
#include "XVariantList.h"
#include "XThread.h"
#include <string.h>
static XSocketNotifierPrivate* XSocketNotifierPrivate_create(XSocketNotifierType type) {
    XSocketNotifierPrivate* d = (XSocketNotifierPrivate*)XMemory_calloc(1, sizeof(XSocketNotifierPrivate));
    if (!d) return NULL;
    d->type = type;
    d->socket = XSocketDescriptor_Invalid();
    return d;
}

static void VXSocketNotifier_deinit(XObject* obj) {
    XSocketNotifier* self = (XSocketNotifier*)obj;
    if (self->d_ptr) {
        XSerialPort_platform_close(self->d_ptr);
        XMemory_free(self->d_ptr);
        self->d_ptr = NULL;
    }
    XClass_Deinit_Parent(XObject, obj);
}

XVtable* XSocketNotifier_class_init(void) {
    XVTABLE_CREAT_DEFAULT
        // 虚函数表初始化
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XObject))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承类
        XVTABLE_INHERIT_DEFAULT(XIODevice_class_init());
    // 重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSocketNotifier_deinit);

#if SHOWCONTAINERSIZE
    printf("XSocketNotifier size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

XSocketNotifier* XSocketNotifier_createWithType(XSocketNotifierType type) {
    if ((type & ~(XSocketNotifier_Read | XSocketNotifier_Write | XSocketNotifier_Exception)) != 0)
        return NULL;

    XSocketNotifier* notifier = (XSocketNotifier*)XMemory_malloc(sizeof(XSocketNotifier));
    if (!notifier) return NULL;

    memset(notifier, 0, sizeof(XSocketNotifier));
    XObject_init(&notifier->base);
    XClassGetVtable(notifier) = XSocketNotifier_class_init();
    notifier->d_ptr = XSocketNotifierPrivate_create(type);
    return notifier;
}

XSocketNotifier* XSocketNotifier_createWithSocket(XSocketDescriptor socket, XSocketNotifierType type) {
    XSocketNotifier* notifier = XSocketNotifier_createWithType(type);
    if (notifier && XSocketDescriptor_isValid(socket)) {
        XSocketNotifier_setSocket(notifier, socket);
    }
    return notifier;
}

void XSocketNotifier_delete(XSocketNotifier* notifier) {
    if (notifier) {
        XObject_delete_base(&notifier->base);
    }
}

void XSocketNotifier_setSocket(XSocketNotifier* notifier, XSocketDescriptor socket) {
    if (!notifier || !notifier->d_ptr) return;
    XSocketNotifierPrivate* d = notifier->d_ptr;
    d->error = XSocketNotifier_NoError;

    if (!XSocketNotifier_platform_setSocket(d, socket, d->type)) {
        d->error = XSocketNotifier_InvalidDescriptor;
    }
}

XSocketDescriptor XSocketNotifier_socket(const XSocketNotifier* notifier) {
    return notifier && notifier->d_ptr ? notifier->d_ptr->socket : XSocketDescriptor_Invalid();
}

XSocketNotifierType XSocketNotifier_type(const XSocketNotifier* notifier) {
    return notifier && notifier->d_ptr ? notifier->d_ptr->type : 0;
}

bool XSocketNotifier_isValid(const XSocketNotifier* notifier) {
    return notifier && notifier->d_ptr && XSocketNotifier_platform_isValid(notifier->d_ptr);
}

bool XSocketNotifier_isEnabled(const XSocketNotifier* notifier) {
    return notifier && notifier->d_ptr && XSocketNotifier_platform_isEnabled(notifier->d_ptr);
}

void XSocketNotifier_setEnabled(XSocketNotifier* notifier, bool enabled) {
    if (notifier && notifier->d_ptr) {
        XSocketNotifier_platform_setEnabled(notifier->d_ptr, enabled);
    }
}

XSocketNotifierError XSocketNotifier_error(const XSocketNotifier* notifier) {
    return notifier && notifier->d_ptr ? notifier->d_ptr->error : XSocketNotifier_NoError;
}

void XSocketNotifier_clearError(XSocketNotifier* notifier) {
    if (notifier && notifier->d_ptr) {
        notifier->d_ptr->error = XSocketNotifier_NoError;
    }
}

void* XSocketNotifier_activated_signal(XSocketNotifier* notifier, XSocketDescriptor socket, XSocketNotifierType type)
{
    if (notifier)
    {
        XVariantList* args = XVariantList_create();
        XVariantList_push_back_move_base(args, XVariant_create_ptr((intptr_t)socket));
        XVariantList_push_back_move_base(args, XVariant_create_int(type));
        XEmitSignal(notifier, XSocketNotifier_activated_signal, args, XVariantList_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return XSocketNotifier_activated_signal;
}