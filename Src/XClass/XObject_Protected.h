/**
 * @file       XObject_Protected.h
 * @brief      XObject 对象基类保护接口（仅供子类与内部实现使用）。
 * @details    本文件集中声明 Qt QObject 中属于 protected 的虚函数分派入口
 *             （event/eventFilter/timerEvent/childEvent/customEvent/
 *             connectNotify/disconnectNotify 的 *_base 调度入口）；
 *             普通用户代码不应直接包含或调用，XObject.h 只保留公开 API。
 *             任何调用这些保护接口的 .c 文件必须显式包含本头文件。
 *             实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或其他平台 API。
 */
#ifndef XOBJECT_PROTECTED_H
#define XOBJECT_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XObject.h"

/* ======================== 受保护的虚函数分派入口（需由子类实现或使用） ======================== */

/**
 * @brief 核心事件处理器（对标 Qt 6.8 QObject::event）
 * @param self 目标对象指针
 * @param e 事件指针
 * @return 事件已被处理返回 true，否则返回 false
 * @note 应在子类初始化时通过函数指针重写
 */
bool XObject_event_base(XObject* self, XEvent* e);

/**
 * @brief 事件过滤器（对标 Qt 6.8 QObject::eventFilter）
 * @param self 目标对象指针
 * @param watched 被监视的对象
 * @param event 事件指针
 * @return 事件已被过滤（不再传播）返回 true，否则返回 false
 * @note watched 对象收到事件时先调用此过滤器
 */
bool XObject_eventFilter_base(XObject* self, XObject* watched, XEvent* event);

/**
 * @brief 定时器事件回调（对标 Qt 6.8 QObject::timerEvent）
 * @param self 目标对象指针
 * @param event 定时器事件指针
 * @note 定时器触发时被调用
 */
void XObject_timerEvent_base(XObject* self, XTimerEvent* event);

/**
 * @brief 子对象事件回调（对标 Qt 6.8 QObject::childEvent）
 * @param self 目标对象指针
 * @param event 子对象事件指针
 * @note 子对象被添加、移除或销毁时被调用
 */
void XObject_childEvent_base(XObject* self, XChildEvent* event);

/**
 * @brief 自定义事件回调（对标 Qt 6.8 QObject::customEvent）
 * @param self 目标对象指针
 * @param event 自定义事件指针
 * @note 用于处理用户自定义的事件类型
 */
void XObject_customEvent_base(XObject* self, XEvent* event);

/**
 * @brief 连接通知（对标 Qt 6.8 QObject::connectNotify）
 * @param self 目标对象指针
 * @param signal 信号标识
 * @note 有信号连接到此对象的槽时被调用
 */
void XObject_connectNotify_base(XObject* self, size_t signal);

/**
 * @brief 断开连接通知（对标 Qt 6.8 QObject::disconnectNotify）
 * @param self 目标对象指针
 * @param signal 信号标识
 * @note 有信号从此对象的槽断开时被调用
 */
void XObject_disconnectNotify_base(XObject* self, size_t signal);

#ifdef __cplusplus
}
#endif
#endif /* XOBJECT_PROTECTED_H */
