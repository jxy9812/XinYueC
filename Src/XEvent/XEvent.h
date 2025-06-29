#ifndef XEVENT_H
#define XEVENT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
#include"XTypes.h"
//#include"XEventDispatcher.h"
//迷你事件
typedef struct XEventMin
{
    bool accept;                  //接受事件
    int code;                     //事件类型代码
    size_t timestamp;             //事件发生时间
    XObject* object;              //接收对象
    void* userData;              // 可选的用户数据指针
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
XEventMin* XEventMin_create(XObject* object,int code, size_t timestamp);
void XEventMin_init(XEventMin* event, XObject* object,int code, size_t timestamp);
XEvent* XEvent_create(void* eventData,size_t eventDataSize);
#define XEvent_AcceptState(event) (((XEventMin*)event)->accept)
#define XEvent_Accept(event) (XEvent_AcceptState(event)=true)
#define XEvent_Ignore(event) (XEvent_AcceptState(event)=false)
#define XEvent_DataPtr(event) (&(((XEvent*)event)->data))
#define XEvent_Data(event,dataType)  (*((dataType*)XEvent_DataPtr(event)))
#define XEvent_Code(event)     (((XEventMin*)event)->code)
#define XEvent_Timestamp(event)     (((XEventMin*)event)->timestamp)
#define XEvent_Object(event)     (((XEventMin*)event)->object)
#define XEvent_UserData(event)     (((XEventMin*)event)->userData)


#ifdef __cplusplus
}
#endif	
#endif // !XDataFrameCommunicatorEvent_H
