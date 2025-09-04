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
// 事件过滤器函数类型
//typedef bool (*XEventFilter)(XObject* obj, XEventMin* event);
// 事件回调函数类型
typedef void (*XEventCB)(XEventMin* event);
//迷你事件
typedef struct XEventMin
{
    bool accept;                  //接受事件
    int code;                     //事件类型代码
    size_t timestamp;             //事件发生时间
    XObject* receiver;            //接收对象
    void* userData;               //可选的用户数据指针
    bool spontaneous;             // 是否为自发事件(非用户触发)
}XEventMin;
// 定时器事件
typedef struct XTimerEvent 
{
    XEventMin event;
    XTimerBase* timer;            // 定时器指针作为ID
} XTimerEvent;
//完整事件
typedef struct XEvent
{
    XEventMin event;
    //size_t id;                  // 事件唯一标识
    uint8_t status;               // 事件状态
    void* data;//事件数据
}XEvent;
/**
 * @brief 创建基础事件
 * @param receiver 事件接收对象
 * @param code 事件类型
 * @param timestamp 时间戳，0表示使用当前时间
 * @param priority 事件优先级
 * @return 新创建的基础事件
 */
XEventMin* XEventMin_create(XObject* receiver, XEventType code, size_t timestamp);
/**
 * @brief 初始化基础事件
 * @param event 要初始化的事件
 * @param receiver 事件接收对象
 * @param code 事件类型
 * @param timestamp 时间戳，0表示使用当前时间
 * @param priority 事件优先级
 */
void XEventMin_init(XEventMin* event, XObject* receiver, XEventType code, size_t timestamp);
/**
 * @brief 创建定时器事件
 * @param receiver 事件接收对象
 * @param timer 定时器指针
 * @param timestamp 时间戳
 * @return 新创建的定时器事件
 */
XTimerEvent* XTimerEvent_create(XObject* receiver, XTimerBase* timer, size_t timestamp);
/**
 * @brief 创建通用事件
 * @param eventData 事件数据
 * @param eventDataSize 事件数据大小
 * @param receiver 事件接收对象
 * @param code 事件类型
 * @param timestamp 时间戳，0表示使用当前时间
 * @param priority 事件优先级
 * @return 新创建的通用事件
 */
XEvent* XEvent_create(void* eventData, size_t eventDataSize, XObject* receiver,
    XEventType code, size_t timestamp);
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
    void (*func)(void* userData); // 需要执行的函数
    void* args;                   // 函数参数
    bool oneAccept;               // 是否执行一次后就接受事件
}XEventFunc;
/**
 * @brief 创建函数事件
 * @param receiver 事件接收对象
 * @param func 要执行的函数
 * @param args 函数参数
 * @param priority 事件优先级
 * @return 新创建的函数事件
 */
XEventFunc* XEventFunc_create(XObject* receiver, void (*func)(void*), void* args);
/**
 * @brief 创建一次性函数事件
 * @param receiver 事件接收对象
 * @param func 要执行的函数
 * @param args 函数参数
 * @param priority 事件优先级
 * @return 新创建的一次性函数事件
 */
XEventFunc* XEventFunc_create_oneAccept(XObject* receiver, void (*func)(void*), void* args);

/**
 * @brief 执行函数事件的回调
 * @param event 函数事件
 */
void XEventFuncRunCB(XEventFunc* event);


//槽函数调用事件
typedef struct XEventSlotFunc
{
    XEventMin event;
    XSlotFunc func;               // 需要执行的槽函数
    XObject* sender;              // 发送者对象
    void* args;                   // 函数参数
    XAtomic_int32_t* ref_count;   // 参数引用计数
}XEventSlotFunc;

/**
 * @brief 创建槽函数事件
 * @param sender 发送者对象
 * @param receiver 接收者对象
 * @param func 槽函数
 * @param args 函数参数
 * @param ref_count 参数引用计数
 * @param priority 事件优先级
 * @return 新创建的槽函数事件
 */
XEventSlotFunc* XEventSlotFunc_create(XObject* sender, XObject* receiver, XSlotFunc func,
    void* args, XAtomic_int32_t* ref_count);

/**
 * @brief 执行槽函数事件的回调
 * @param event 槽函数事件
 */
void XEventSlotFuncRunCB(XEventSlotFunc* event);
#ifdef __cplusplus
}
#endif	
#endif // !XDataFrameCommunicatorEvent_H
