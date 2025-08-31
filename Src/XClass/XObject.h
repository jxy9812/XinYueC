#ifndef XOBJECT_H
#define XOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XClass.h"
#include"XEvent.h"
#include"XSignalSlot.h"
XCLASS_DEFINE_BEGING(XObject)
XCLASS_DEFINE_ENUM(XObject, Poll) = XCLASS_VTABLE_GET_SIZE(XClass),
//XCLASS_DEFINE_ENUM(XObject, AddEventFilter),
XCLASS_DEFINE_END(XObject)
typedef struct XObject
{
    XClass m_parent;//父对象
    XEventDispatcher* m_eventDispatcher; // 事件调度器
    XSignalSlot* m_signalSlot;//信号与槽控制
}XObject;//
XVtable* XObject_class_init();
XObject* XObject_create();
void XObject_init(XObject* object);
void XObject_poll_base(XObject* object);
bool XObject_addEventFilter(XObject* object, int code, XEventCB cb,void* userData);
bool XObject_removeEventFilter(XObject* object, int code);
bool XObject_moveToThread(XObject* object, XThread* thread);
//给Object投递事件
bool XObject_postEvent(XObject* object, XEventMin* event);
XThread* XObject_thread(XObject* object);
XEventDispatcher* XObject_getEventDispatcher(XObject* object);
//信号与槽
XConnection* XObject_connect(XObject* object, size_t signal, XObject* receiver, XSlotFunc slot_func, XConnectionType type);

bool XObject_disconnect(XObject* object, size_t signal, XObject* receiver, XSlotFunc slot_func);
bool XObject_disconnect_conn(XConnection* conn);
//slot: void deinit_slot(XObject* sender, XObject* receiver, void* args)
void* XObject_deinit_signal(XObject* object);

#define XObject_deinit_base    XClass_deinit_base
#define XObject_delete_base    XClass_delete_base
//事件中调用延迟释放
void XObject_deinit_event(XObject* object);
void XObject_delete_event(XObject* object);
#ifdef __cplusplus
}
#endif
#endif