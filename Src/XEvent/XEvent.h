#ifndef XEVENT_H
#define XEVENT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
#include"XTypes.h"
#include"XEventType.h"
#include"XSignalSlot.h"
#include"XAtomic.h"
//事件回调函数
typedef void (*XEventCB)(XEventMin* event);
//迷你事件
typedef struct XEventMin
{
    bool accept;                  //接受事件
    int code;                     //事件类型代码
    size_t timestamp;             //事件发生时间
    XObject* receiver;            //接收对象
    void* userData;               //可选的用户数据指针
}XEventMin;

//完整事件
typedef struct XEvent
{
    XEventMin event;
    //size_t id;                  // 事件唯一标识
    uint8_t status;               // 事件状态
    void* data;//事件数据
}XEvent;
//创建一个迷你事件
XEventMin* XEventMin_create(XObject* receiver,int code, size_t timestamp);
void XEventMin_init(XEventMin* event, XObject* receiver,int code, size_t timestamp);
XEvent* XEvent_create(void* eventData,size_t eventDataSize);
#define XEvent_AcceptState(event)               (((XEventMin*)event)->accept)
#define XEvent_Accept(event)                    (XEvent_AcceptState(event)=true)
#define XEvent_Ignore(event)                    (XEvent_AcceptState(event)=false)
#define XEvent_DataPtr(event)                   (&(((XEvent*)event)->data))
#define XEvent_Data(event,dataType)             (*((dataType*)XEvent_DataPtr(event)))
#define XEvent_Code(event)                      (((XEventMin*)event)->code)
#define XEvent_Timestamp(event)                 (((XEventMin*)event)->timestamp)
#define XEvent_Receiver(event)                  (((XEventMin*)event)->receiver)
#define XEvent_UserData(event)                  (((XEventMin*)event)->userData)

//函数运行事件
typedef struct XEventFunc
{
    XEventMin event;
    void (*func)(void* userData);//需要执行的函数
    void* args;//参数
    bool oneAccept;
}XEventFunc;
XEventFunc* XEventFunc_create(XObject* receiver, void (*func)(void*),void* args);
//创建函数事件执行一次就接收不在继续传播
XEventFunc* XEventFunc_create_oneAccept(XObject* receiver, void (*func)(void*), void* args);
//函数执行回调
void XEventFuncRunCB(XEventFunc* event);//XEVENT_FUNC_RUN


//槽函数调用事件
typedef struct XEventSlotFunc
{
    XEventMin event;
    XSlotFunc func;//需要执行的函数
    XObject* sender;
    void* args;//参数
    XAtomic_int32_t* ref_count;//参数是否是XVariant 只读引用
}XEventSlotFunc;

XEventSlotFunc* XEventSlotFunc_create(XObject* sender,XObject* receiver, XSlotFunc func, void* args, XAtomic_int32_t* ref_count);
//函数执行回调
void XEventSlotFuncRunCB(XEventSlotFunc* event);//XEVENT_FUNC_RUN
#ifdef __cplusplus
}
#endif	
#endif // !XDataFrameCommunicatorEvent_H
