#ifndef XEVENTTRANSITION_H
#define XEVENTTRANSITION_H

#include "XAbstractTransition.h"
#include "XEventType.h"

/**
 * @brief 事件类型触发的转换类
 */
typedef struct XEventTransition {
    XAbstractTransition parent;  // 继承XAbstractTransition
    XEventType eventType;        // 事件类型
} XEventTransition;

/**
 * @brief 创建事件转换实例
 * @param eventType 事件类型
 * @return 新创建的事件转换实例，失败返回NULL
 */
XEventTransition* XEventTransition_create(XEventType eventType);

/**
 * @brief 初始化事件转换
 * @param transition 事件转换实例
 * @param eventType 事件类型
 */
void XEventTransition_init(XEventTransition* transition, XEventType eventType);

/**
 * @brief 销毁事件转换
 * @param transition 事件转换实例
 */
void XEventTransition_destroy(XEventTransition* transition);

/**
 * @brief 获取事件类型
 * @param transition 事件转换实例
 * @return 事件类型
 */
XEventType XEventTransition_eventType(const XEventTransition* transition);

/**
 * @brief 设置事件类型
 * @param transition 事件转换实例
 * @param eventType 事件类型
 */
void XEventTransition_setEventType(XEventTransition* transition, XEventType eventType);

/**
 * @brief 处理事件
 * @param transition 事件转换实例
 * @param machine 状态机
 * @param event 事件
 * @return 事件被处理返回true，否则返回false
 */
bool XEventTransition_processEvent(XEventTransition* transition, XStateMachine* machine, const XEvent* event);

#endif // XEVENTTRANSITION_H