// XThread_p.h - 内部使用
#ifndef XTHREADDATA_H
#define XTHREADDATA_H
#include "XAbstractEventDispatcher.h"
#include "XMutex.h"
#include "XVector.h" // 假设你有 XVector 实现（基于 realloc）
#include "XStack.h"
#include "XLockFreeQueue.h"
#include "XAtomic.h"
#include "XSemaphore.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    XObject* receiver;
    XEvent* event;
    int      priority;  // 任意整数，越大优先级越高
} XPostEvent;

/**
 * @brief 发送者栈帧 - 记录当前线程正在派发的信号发送者(对标 Qt 的 per-thread sender 栈)
 *        取代 XObject::m_sender 的每对象状态,消除跨线程竞争与嵌套发射时 sender() 错乱
 */
typedef struct {
    XObject* receiver;  // 接收者(槽所属对象)
    XObject* sender;    // 信号发送者
} XSenderFrame;

/**
 * @brief XThreadData - 每个线程的私有数据（对标 QThreadData）
 */
typedef struct XThreadData{
    XMutex* m_mutex; // 保护 postEventList
    XThread* m_thread;
    XAbstractEventDispatcher* m_dispatcher;   // 本线程的事件分发器
    XAtomic_uintptr_t m_currentEventLoop;//当前正在运行的事件循环
    XAtomic_size_t m_loopLevel; // <-- 关键：一个原子整数计数器
    XLockFreeQueue/*<XPostEvent>*/   m_tryPostEventList;  //无锁投递队列
    XVector/*<XPostEvent>*/ m_postEventList;  //互斥锁投递队列
    XSemaphore* m_wakeSemaphore;       //线程局部唤醒信号量（工作线程阻塞等待用）
    XStack/*<XSenderFrame>*/ m_senderStack;  //每线程发送者栈(Direct/队列连接派发槽函数时压栈,XObject_sender 据此返回当前发送者)
    //XVector/*<XPostEvent>*/ m_handlerEventList;  //事件处理专用队列
} XThreadData;

//需平台实现
XAbstractEventDispatcher* XEventDispatcher_create(XObject* parent);

XThreadData* XThreadData_create(XThread* thread);
void XThreadData_delete(XThreadData* data);
void XThreadData_init(XThreadData* data,XThread* thread);

// 获取当前线程的 XThreadData
XThreadData* XThreadData_current(void);
XHandle XThreadData_mapInsert(XThreadData* data);
void XThreadData_mapRemove(XHandle id);
// 初始化主线程的 XThreadData（由 XCoreApplication 调用）
XThreadData* XThreadData_initMainThread(XThread* thread);
XThreadData* XThreadData_mainThread();
XEventLoop* XThreadData_currentEventLoop(XThreadData* data);
void XThreadData_pushEventloop(XThreadData* data, XEventLoop*loop);
void XThreadData_popEventloop(XThreadData* data, XEventLoop* loop);

// 向当前线程投递事件（内部使用）
void XThreadData_postEvent(XObject* receiver, XEvent* event, int priority);
//无锁投递,设计用在中断中,多线程并发无法保证顺序
bool XThreadData_tryPostEvent(XObject* receiver, XEvent* event, int priority);
//向当前线程事件队列头部追加未处理的事件列表传入 XThreadData_takePostedEvents(void) 的返回值
void XThreadData_push_front_list(const XVector* events);
// 消费并清空当前线程的 posted events（副本自己管理）
XVector*/*<XPostEvent>*/ XThreadData_takePostedEvents(void);

// 设置当前线程的事件分发器
void XThreadData_setEventDispatcher(XAbstractEventDispatcher* dispatcher);

// 等待唤醒信号（工作线程阻塞等待用，超时返回）
void XThreadData_waitForWake(XThreadData* td, int timeoutMs);
// 发出唤醒信号（跨线程唤醒目标线程）
void XThreadData_signalWake(XThreadData* td);

// 发送者栈:Direct/队列连接派发槽函数前压栈,派发后弹栈;XObject_sender() 据此查询当前发送者
// 未注册 XThreadData 的线程(无事件循环)调用为空操作,此时 sender() 返回 NULL(与 Qt 一致)
void XThreadData_pushSender(XObject* receiver, XObject* sender);
void XThreadData_popSender(void);
XObject* XThreadData_currentSender(XObject* receiver);  // 返回栈顶 receiver==receiver 的帧的 sender

#ifdef __cplusplus
}
#endif

#endif // XTHREAD_P_H