#ifndef XEVENTDISPATCHERTHREAD_H
#define XEVENTDISPATCHERTHREAD_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XEventDispatcher.h"
#include"XQueueBase.h"
#include"XMapBase.h"

XCLASS_DEFINE_BEGING(XEventDispatcherThread)
XCLASS_DEFINE_ENUM(XEventDispatcherThread, AddObject) = XCLASS_VTABLE_GET_SIZE(XEventDispatcher),
XCLASS_DEFINE_ENUM(XEventDispatcherThread, RemoveObject),
XCLASS_DEFINE_ENUM(XEventDispatcherThread, IsEmptyObject),
XCLASS_DEFINE_END(XEventDispatcherThread)

/*                      事件调度器                  ~                               */
typedef  struct XEventDispatcherThread
{
    XEventDispatcher m_parent;//父对象
    XSetBase* m_Objects;//列表
}XEventDispatcherThread;
XVtable* XEventDispatcherThread_class_init();
XEventDispatcherThread* XEventDispatcherThread_create(size_t queueCount);
void XEventDispatcherThread_init(XEventDispatcherThread* dispatcher);
bool XEventDispatcherThread_addEventCb_base(XEventDispatcherThread* dispatcher,XObject* object, int code, XEventCB cb,void* userData);
bool XEventDispatcherThread_removeEventCb_base(XEventDispatcherThread* dispatcher, XObject* object,int code);

bool XEventDispatcherThread_addObject_base(XEventDispatcherThread* dispatcher, XObject* object);
bool XEventDispatcherThread_removeObject_base(XEventDispatcherThread* dispatcher, XObject* object);
bool XEventDispatcherThread_isEmptyObject_base(XEventDispatcherThread* dispatcher);
#define XEventDispatcherThread_sendEvent_base               XEventDispatcher_sendEvent_base
#define XEventDispatcherThread_postEvent_base               XEventDispatcher_postEvent_base
#define XEventDispatcherThread_handler_base                 XEventDispatcher_handler_base
#define XEventDispatcherThread_delete_base                  XClass_delete_base
#ifdef __cplusplus
}
#endif	
#endif