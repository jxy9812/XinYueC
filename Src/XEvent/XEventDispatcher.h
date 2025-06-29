#ifndef XEVENTDISPATCHER_H
#define XEVENTDISPATCHER_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XQueueBase.h"
#include"XMapBase.h"
//事件回调函数
typedef void (*XEventCB)(XEventMin* event);
/*                      事件调度器                                                 */
typedef  struct XEventDispatcher
{
    XQueueBase* m_queue;//用来处理事件
    XMapBase* m_filter_cb;//事件过滤回调
    XEventCB m_allEvent_cb;//全部事件的回调
    void* m_allEvent_user_data;//全部事件触发的回调用户数据
    XListBase* m_pollList;//轮询链表
}XEventDispatcher;

XEventDispatcher* XEventDispatcher_create(XQueueBase* queue, XMapBase* map_cb);
XEventDispatcher* XEventDispatcher_createDefault(size_t queueCount);
//添加一个事件
bool XEventDispatcher_addEvent(XEventDispatcher* dispatcher, XEventMin* event);
//释放内存
void XEventDispatcher_delete(XEventDispatcher* dispatcher);
//添加事件回调
bool XEventDispatcher_addEventCb(XEventDispatcher* dispatcher, XEventCB cb, int code, void* userData);
bool XEventDispatcher_removeEventCb(XEventDispatcher* dispatcher, int code);
bool XEventDispatcher_setAllEventCb(XEventDispatcher* dispatcher, XEventCB cb, void* userData);
//事件轮询处理
void XEventDispatcher_handler(XEventDispatcher* dispatcher);
bool XEventDispatcher_addObject(XEventDispatcher* dispatcher, XObject* object);
bool XEventDispatcher_removeObject(XEventDispatcher* dispatcher, XObject* object);
size_t XEventDispatcher_getObjectSize(XEventDispatcher* dispatcher);


#ifdef __cplusplus
}
#endif	
#endif