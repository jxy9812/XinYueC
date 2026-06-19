// XSocketNotifier.c
#include "XSocketNotifier.h"
#include "XMemory.h"
#include "XVariant.h"
#include "XVariantList.h"
#include "XThread.h"
#include "XAbstractEventDispatcher.h"
#include <string.h>

/**
 * @brief XSocketNotifier 完整定义（用户不应直接访问成员）
 */


// Internal forward declaration
static void VXSocketNotifier_deinit(XObject* obj);
XVtable* XSocketNotifier_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XSocketNotifier))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
    //继承类
    XVTABLE_INHERIT_XCLASS(XObject);
 /*   void* table[] = { VXObject_poll };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);*/
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSocketNotifier_deinit);
#if SHOWCONTAINERSIZE
    printf("XSocketNotifier size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

void VXSocketNotifier_deinit(XObject* obj)
{
    XSocketNotifier* self = (XSocketNotifier*)obj;
    // 注销（由 dispatcher 实现，安全处理无效状态）
    XAbstractEventDispatcher_unregisterSocketNotifier_base(XThread_currentDispatcher(), self);
    XClass_Deinit_Parent(XObject, obj);
}

XSocketNotifier* XSocketNotifier_createWithType(XSocketNotifierType type)
{
    // 验证 type 合法性（Qt 允许组合，但通常单选；此处宽松处理）
    if ((type & ~(XSocketNotifier_Read | XSocketNotifier_Write | XSocketNotifier_Exception)) != 0) {
        return NULL;
    }

    XSocketNotifier* notifier = (XSocketNotifier*)XCalloc_System(1, sizeof(XSocketNotifier));
    if (!notifier) return NULL;
    XSocketNotifier_init(notifier,type);
    Set_Class_MemoryFree(notifier, XFree_System);
    return notifier;
}

XSocketNotifier* XSocketNotifier_createWithSocket(XSocketDescriptor socket, XSocketNotifierType type)
{
    XSocketNotifier* notifier = XSocketNotifier_createWithType(type);
    if (notifier && XSocketDescriptor_isValid(socket)) {
        XSocketNotifier_setSocket(notifier, socket);
    }
    return notifier;
}

void XSocketNotifier_init(XSocketNotifier* notifier, XSocketNotifierType type)
{
    if (!notifier)return;
    XObject_init(notifier);
    XClassGetVtable(notifier) = XSocketNotifier_class_init();

    notifier->socket = XSocketDescriptor_Invalid();
    if ((type & ~(XSocketNotifier_Read | XSocketNotifier_Write | XSocketNotifier_Exception)) != 0) {
        notifier->type = XSocketNotifier_ReadWrite;
    }
    else
    {
        notifier->type = type;
    }
    notifier->enabled = true; // Qt 默认启用
}

void XSocketNotifier_setSocket(XSocketNotifier* notifier, XSocketDescriptor socket)
{
    if (!notifier) return;
    // 先注销旧的（如果有效）
    if (XSocketDescriptor_isValid(notifier->socket)) {
        XAbstractEventDispatcher_unregisterSocketNotifier_base(XThread_currentDispatcher(), notifier);
    }

    notifier->socket = socket;

    // 如果新 socket 有效且已启用，则注册
    if (notifier->enabled && XSocketDescriptor_isValid(socket)) {
        XAbstractEventDispatcher_registerSocketNotifier_base(XThread_currentDispatcher(), notifier);
    }
}

XSocketDescriptor XSocketNotifier_socket(const XSocketNotifier* notifier)
{
    return notifier ? notifier->socket : XSocketDescriptor_Invalid();
}

XSocketNotifierType XSocketNotifier_type(const XSocketNotifier* notifier)
{
    return notifier ? notifier->type : 0;
}

bool XSocketNotifier_isValid(const XSocketNotifier* notifier)
{
    return notifier && XSocketDescriptor_isValid(notifier->socket);
}

bool XSocketNotifier_isEnabled(const XSocketNotifier* notifier)
{
    return notifier ? notifier->enabled : false;
}

void XSocketNotifier_setEnabled(XSocketNotifier* notifier, bool enabled)
{
    if (!notifier) return;
    if (notifier->enabled == enabled) return;

    notifier->enabled = enabled;

    if (enabled && XSocketDescriptor_isValid(notifier->socket)) {
        XAbstractEventDispatcher_registerSocketNotifier_base(XThread_currentDispatcher(), notifier);
    }
    else {
        XAbstractEventDispatcher_unregisterSocketNotifier_base(XThread_currentDispatcher(), notifier);
    }
}

void* XSocketNotifier_activated_signal(XSocketNotifier* notifier, XSocketDescriptor socket, XSocketNotifierType type)
{
    XEmitSignal(notifier, XSocketNotifier_activated_signal, XVarList_Create(XVar(XSocketDescriptor, socket),XVar(XSocketNotifierType, type)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}