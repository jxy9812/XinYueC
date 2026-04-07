// XThread_p.h - 内部使用
#ifndef XTHREADDATA_H
#define XTHREADDATA_H

//#include "XPostEvent.h"
#include "XAbstractEventDispatcher.h"
#include "XMutex.h"
#include "XVector.h" // 假设你有 XVector 实现（基于 realloc）

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    XObject* receiver;
    XEvent* event;
    int      priority;  // 任意整数，越大优先级越高
} XPostEvent;
/**
 * @brief XThreadData - 每个线程的私有数据（对标 QThreadData）
 */
typedef struct XThreadData{
    XMutex* m_mutex; // 保护 postEventList
    XThread* m_thread;
    XVector/*<XPostEvent>*/ m_postEventList;  // 动态数组
    XAbstractEventDispatcher* m_dispatcher;   // 本线程的事件分发器
} XThreadData;
//需平台实现
XAbstractEventDispatcher* XEventDispatcher_create();
XThreadData* XThreadData_create(XThread* thread);
void XThreadData_delete(XThreadData* data);
void XThreadData_init(XThreadData* data,XThread* thread);

// 获取当前线程的 XThreadData
XThreadData* XThreadData_current(void);
void XThreadData_mapInsert(XThreadData* data);
void XThreadData_mapRemove(XThreadData* data);
// 初始化主线程的 XThreadData（由 XCoreApplication 调用）
void XThreadData_initMainThread(void);

// 向当前线程投递事件（内部使用）
void XThreadData_postEvent(XObject* receiver, XEvent* event, int priority);

// 消费并清空当前线程的 posted events（返回局部副本）
XVector*/*<XPostEvent>*/ XThreadData_takePostedEvents(void);

// 设置当前线程的事件分发器
void XThreadData_setEventDispatcher(XAbstractEventDispatcher* dispatcher);

#ifdef __cplusplus
}
#endif

#endif // XTHREAD_P_H