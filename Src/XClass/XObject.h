#ifndef XOBJECT_H
#define XOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XClass.h"
#include"XEvent.h"
XCLASS_DEFINE_BEGING(XObject)
XCLASS_DEFINE_ENUM(XObject, Poll) = XCLASS_VTABLE_GET_SIZE(XClass),
//XCLASS_DEFINE_ENUM(XObject, AddEventFilter),
XCLASS_DEFINE_END(XObject)
typedef struct XObject
{
    XClass m_parent;//父对象
    XEventDispatcherThread* m_eventDispatcher; // 事件调度器
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
XEventDispatcherThread* XObject_getEventDispatcher(XObject* object);
#define XObject_delete_base    XClass_delete_base
#ifdef __cplusplus
}
#endif
#endif