#ifndef XEVENTQUEUE_H
#define XEVENTQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
//宏定义
#ifndef XEventQueueDefaultConfig
#define XEventQueueDefaultConfig 1   //启动默认配置
#endif // !XEventQueueDefaultConfig
#ifndef XEventQueueEventType         
#define XEventQueueEventType int    //事件类型
#endif // !XEventQueueEventType
//配置一些自定义函数类型
typedef struct XEventQueue XEventQueue;
typedef bool (*XEventQueueInit)(XEventQueue* queue);
typedef void (*XEventQueueFree)(XEventQueue* queue);
typedef bool (*XEventQueuePush)(XEventQueue* queue, XEventQueueEventType event);
typedef XEventQueueEventType(*XEventQueueTop)(XEventQueue* queue);
typedef bool (*XEventQueuePop)(XEventQueue* queue);
typedef bool(*XEventQueueEmpty)(XEventQueue* queue);
typedef void(*XEventQueueClear)(XEventQueue* queue);
//事件队列
typedef struct XEventQueue
{
	void* queue;
	XEventQueueFree free;
	XEventQueuePush push;
	XEventQueueTop top;
	XEventQueuePop pop;
	XEventQueueEmpty empty;
	XEventQueueClear clear;
}XEventQueue;
XEventQueue* XEventQueue_new(XEventQueueInit init);
//
#if XEventQueueDefaultConfig
bool XEventQueue_defaultConfigInit(XEventQueue* queue);
#endif 
void XEventQueue_free(XEventQueue* queue);
bool XEventQueue_push(XEventQueue* queue, XEventQueueEventType event);
XEventQueueEventType XEventQueue_Top(XEventQueue* queue);
bool XEventQueue_pop(XEventQueue* queue);
bool XEventQueue_empty(XEventQueue* queue);
void XEventQueue_clear(XEventQueue* queue);
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
