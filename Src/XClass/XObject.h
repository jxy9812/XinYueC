#ifndef XOBJECT_H
#define XOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XClass.h"
#include"XEvent.h"
#include"XSignalSlot.h"
XCLASS_DEFINE_BEGING(XObject)
XCLASS_DEFINE_ENUM(XObject, Poll) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_END(XObject)
typedef struct XObject
{
    XClass m_class;//继承类
    bool m_isEventBubblingEnabled;//事件冒泡
    XObject* m_parent;//父对象
    XThread* m_thread;//所属线程
    XSignalSlot* m_signalSlot;//信号与槽控制
    XTimerBase* m_poolTimer;//轮询定时器
}XObject;//
XVtable* XObject_class_init();
XObject* XObject_create();
void XObject_init(XObject* object);
//轮询函数
void XObject_poll_base(XObject* object);
//设置轮询间隔
void XObject_setPollingInterval(XObject* object,size_t interval);
void XObject_setParent(XObject* object, XObject* parent);
XObject* XObject_getParent(XObject* object);
//是否开启事件冒泡
void XObject_setEventBubblingEnabled(XObject* object, bool enable);//处理事件未被接受则会转发到父对象
bool XObject_isEventBubbling(XObject* object);
//
bool XObject_addEventFilter(XObject* object, int code, XEventCB cb,void* userData);
bool XObject_removeEventFilter(XObject* object, int code);
bool XObject_moveToThread(XObject* object, XThread* thread);
//给Object投递事件
bool XObject_postEvent(XObject* object, XEvent* event, XEventPriority priority);
//给Object投递函数(ps异步,将在事件循环中执行)
bool XObject_postFunc(XObject* object, void (*func)(void*), void* args,XEventSendMode mode, XEventPriority priority);
XThread* XObject_thread(XObject* object);
XEventDispatcher* XObject_getEventDispatcher(XObject* object);
XEventLoop* XObject_getEventLoop(XObject* object);
//信号与槽
XConnection* XObject_connect(XObject* object, size_t signal, XObject* receiver, XSlotFunc slot_func, XConnectionType type);
//信号绑定辅助宏
#define XSignal(signal)      ((void*(*)(XObject*))signal)(NULL)
bool XObject_disconnect(XObject* object, size_t signal, XObject* receiver, XSlotFunc slot_func);
bool XObject_disconnect_conn(XConnection* conn);
/**
 * @brief 为信号设置发送模式(设置为队列模式后,可以在中断中发送信号,无锁发送)
 * @param object 对象实例（不能为空）
 * @param mode 要设置的发送模式（XEventSendMode枚举值）
 * @return true表示设置成功，false表示失败（如manager为空、signal无效等）
 */
bool XObject_setSignalSendMode(XObject* object, XEventSendMode mode);
XEventSendMode XObject_getSignalSendMode(XObject* object);

#define XObject_deinit_base    XClass_deinit_base
#define XObject_delete_base    XClass_delete_base
void XObject_emitSignal(XObject* object, size_t signal, void* args, XEventPriority priority);
void XObject_emitSignal_class(XObject* object, size_t signal, XClass* args, XAtomic_int32_t* ref_count, XEventPriority priority);
//slot: void deinit_slot(XObject* m_sender, XObject* receiver, void* args)
void* XObject_deinit_signal(XObject* object);

//事件中调用延迟释放
void XObject_deinit_event(XObject* object);
void XObject_delete_event(XObject* object);
#ifdef __cplusplus
}
#endif
#endif