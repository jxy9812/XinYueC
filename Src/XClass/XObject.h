#ifndef XOBJECT_H
#define XOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XClass.h"
XCLASS_DEFINE_BEGING(XObject)
XCLASS_DEFINE_ENUM(XObject, Poll) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_END(XObject)
typedef struct XObject
{
    XClass m_parent;//父对象
    XSetBase* m_Objects;//列表
    XEventDispatcher* m_eventDispatcher; // 事件调度器
}XObject;//
XVtable* XObject_class_init();
XObject* XObject_create();
void XObject_init(XObject* object);
void XObject_poll_base(XObject* object);
XEventDispatcher* XObject_getEventDispatcher(XObject* object);
#define XObject_delete_base    XClass_delete_base
#ifdef __cplusplus
}
#endif
#endif