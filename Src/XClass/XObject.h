#ifndef XOBJECT_H
#define XOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XClass.h"
//
typedef struct XObject
{
    XClass m_parent;//父对象
    XEventDispatcher* m_eventDispatcher; // 事件调度器
}XObject;//
XVtable* XObject_class_init();
XObject* XObject_create();
void XObject_init(XObject* object);
#ifdef __cplusplus
}
#endif
#endif