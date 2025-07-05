#ifndef XEVENTDISPATCHER_H
#define XEVENTDISPATCHER_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XClass.h"
#include"XQueueBase.h"
#include"XMapBase.h"
#include"XEvent.h"
XCLASS_DEFINE_BEGING(XEventDispatcher)
XCLASS_DEFINE_ENUM(XEventDispatcher, SendEvent) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XEventDispatcher, PostEvent),
XCLASS_DEFINE_ENUM(XEventDispatcher, AddEventCb),
XCLASS_DEFINE_ENUM(XEventDispatcher, RemoveEventCb),
XCLASS_DEFINE_ENUM(XEventDispatcher, Handler),
XCLASS_DEFINE_END(XEventDispatcher)
/*                      事件调度器                                                 */
typedef  struct XEventDispatcher
{
    XClass m_parent;//父对象
    XQueueBase* m_queue;//用来处理事件
    XMapBase* m_filter_cb;//事件过滤回调
}XEventDispatcher;
XVtable* XEventDispatcher_class_init();
XEventDispatcher* XEventDispatcher_create(XQueueBase* queue, XMapBase* map_cb);
XEventDispatcher* XEventDispatcher_createDefault(size_t queueCount);
void XEventDispatcher_init(XEventDispatcher* dispatcher);
bool XEventDispatcher_sendEvent_base(XEventDispatcher* dispatcher, XEventMin* event);
bool XEventDispatcher_postEvent_base(XEventDispatcher* dispatcher, XEventMin* event);
bool XEventDispatcher_addEventCb_base(XEventDispatcher* dispatcher, int code, XEventCB cb, void* userData);
bool XEventDispatcher_removeEventCb_base(XEventDispatcher* dispatcher,int code);
void XEventDispatcher_handler_base(XEventDispatcher* dispatcher);

#define XEventDispatcher_delete_base        XClass_delete_base
#ifdef __cplusplus
}
#endif	
#endif