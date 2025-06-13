#ifndef XEVENT_H
#define XEVENT_H
#include<stdio.h>
#include<stdint.h>
#include"XQueueBase.h"
#include"XMapBase.h"
//迷你事件
typedef struct XEventMin
{
    bool accept;                  //接受事件
    int code;                     //事件类型代码
    size_t timestamp;             //事件发生时间
    void* userData;              // 可选的用户数据指针
}XEventMin;
//事件回调函数
typedef void (*XEventCB)(XEventMin* event);
//完整事件
typedef struct XEvent
{
    XEventMin event;
    //size_t id;                  // 事件唯一标识
    uint8_t status;               // 事件状态
    void* data;//事件数据
}XEvent;
//创建一个迷你事件
XEventMin* XEventMin_create(int code, size_t timestamp);
void XEventMin_init(XEventMin* event,int code, size_t timestamp);
XEvent* XEvent_create(void* eventData,size_t eventDataSize);
#define XEvent_AcceptState(event) (((XEventMin*)event)->accept)
#define XEvent_Accept(event) (XEvent_AcceptState(event)=true)
#define XEvent_Ignore(event) (XEvent_AcceptState(event)=false)
#define XEvent_DataPtr(event) (&(((XEvent*)event)->data))
#define XEvent_Data(event,dataType)  (*((dataType*)XEvent_DataPtr(event)))
#define XEvent_Code(event)     (((XEventMin*)event)->code)
#define XEvent_Timestamp(event)     (((XEventMin*)event)->timestamp)
#define XEvent_UserData(event)     (((XEventMin*)event)->userData)
/*                      事件调度器                                                 */        
typedef  struct XEventDispatcher
{
    XQueueBase* m_queue;//用来处理事件
    XMapBase* m_filter_cb;//事件过滤回调
    XEventCB m_allEvent_cb;//全部事件的回调
    void* m_allEvent_user_data;//全部事件触发的回调用户数据
}XEventDispatcher;

XEventDispatcher* XEventDispatcher_create(XQueueBase* queue, XMapBase* map_cb);
XEventDispatcher* XEventDispatcher_createDefault(size_t queueCount);
//添加一个事件
bool XEventDispatcher_addEvent(XEventDispatcher* dispatcher, XEventMin* event);
//释放内存
void XEventDispatcher_delete(XEventDispatcher* dispatcher);
//添加事件回调
bool XEventDispatcher_addEventCb(XEventDispatcher* dispatcher, XEventCB cb,int code, void* userData);
bool XEventDispatcher_removeEventCb(XEventDispatcher* dispatcher, int code);
bool XEventDispatcher_setAllEventCb(XEventDispatcher* dispatcher, XEventCB cb,void* userData);
//事件轮询处理
void XEventDispatcher_handler(XEventDispatcher* dispatcher);



#endif // !XDataFrameCommunicatorEvent_H
