#ifndef XEVENT_H
#define XEVENT_H
#include<stdio.h>
#include<stdint.h>
#include"XQueueBase.h"

typedef struct XEvent
{
    int type;                     //事件类型
    size_t timestamp;             //事件发生时间
    //size_t id;                  // 事件唯一标识
    uint8_t status;               // 事件状态
    void* user_data;              // 可选的用户数据指针
    void (*callback)(void*);      // 可选的回调函数
    void* data;//事件数据
}XEvent;
XEvent* XEvent_create(size_t eventDataSize);
#define XEvent_DataPtr(event) (&(((XEvent*)event)->data))
#define XEvent_Data(event,dataType)  (*((dataType*)XEvent_DataPtr(event)))

typedef XQueueBase XEventQueue ;
//XEventQueue* XEventQueue_create();
//入队一个事件
bool XEventQueue_pushEvent(XEventQueue* queue, XEvent* event);
//接收事件并且出队
bool XEventQueue_receive(XEventQueue* queue, void* pvBuffer);
//释放内存
void XEventQueue_delete(XEventQueue* queue);


#endif // !XDataFrameCommunicatorEvent_H
