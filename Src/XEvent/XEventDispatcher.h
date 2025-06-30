#ifndef XEVENTDISPATCHER_H
#define XEVENTDISPATCHER_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XClass.h"
#include"XQueueBase.h"
#include"XMapBase.h"
XCLASS_DEFINE_BEGING(XEventDispatcher)
XCLASS_DEFINE_ENUM(XEventDispatcher, SendEvent) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XEventDispatcher, PostEvent),
XCLASS_DEFINE_ENUM(XEventDispatcher, AddEventCb),
XCLASS_DEFINE_ENUM(XEventDispatcher, RemoveEventCb),
XCLASS_DEFINE_ENUM(XEventDispatcher, SetAllEventCb),
XCLASS_DEFINE_END(XEventDispatcher)
//事件回调函数
typedef void (*XEventCB)(XEventMin* event);
/*                      事件调度器                                                 */
typedef  struct XEventDispatcher
{
    XClass m_parent;//父对象
    XQueueBase* m_queue;//用来处理事件
    XMapBase* m_filter_cb;//事件过滤回调
    XEventCB m_allEvent_cb;//全部事件的回调
    void* m_allEvent_user_data;//全部事件触发的回调用户数据
}XEventDispatcher;
XVtable* XEventDispatcher_class_init();
XEventDispatcher* XEventDispatcher_create(XQueueBase* queue, XMapBase* map_cb);
XEventDispatcher* XEventDispatcher_createDefault(size_t queueCount);
void XEventDispatcher_init(XEventDispatcher* dispatcher);
//添加一个事件
bool XEventDispatcher_addEvent(XEventDispatcher* dispatcher, XEventMin* event);
//添加事件回调
bool XEventDispatcher_addEventCb(XEventDispatcher* dispatcher, XEventCB cb, int code, void* userData);
bool XEventDispatcher_removeEventCb(XEventDispatcher* dispatcher, int code);
bool XEventDispatcher_setAllEventCb(XEventDispatcher* dispatcher, XEventCB cb, void* userData);
//事件轮询处理
void XEventDispatcher_handler(XEventDispatcher* dispatcher);

#define XEventDispatcher_delete_base        XClass_delete_base
#ifdef __cplusplus
}
#endif	
#endif