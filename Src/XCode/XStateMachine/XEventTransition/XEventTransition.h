#ifndef XEVENTTRANSITION_H
#define XEVENTTRANSITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XAbstractTransition.h"

XCLASS_DEFINE_BEGING(XEventTransition)
XCLASS_DEFINE_EXTEND_END(XEventTransition, XAbstractTransition);

/** @brief 由指定 XObject 事件触发的转换，对应 Qt 6.8 的 QEventTransition。 */
typedef struct XEventTransition {
    XAbstractTransition m_class; ///< 继承 XAbstractTransition。
    XObject* m_eventSource;      ///< 被监听的事件源，不取得所有权。
    XEventType m_eventType;      ///< 匹配的事件类型。
    bool m_registered;           ///< 事件过滤器是否已经注册。
} XEventTransition;

/**
 * @brief 初始化 XEventTransition 虚函数表。
 * @return XEventTransition 的共享虚函数表。
 */
XVtable* XEventTransition_class_init(void);
/**
 * @brief 创建未指定事件源的事件转换。
 * @return 新事件转换；内存分配失败时返回 NULL。
 */
XEventTransition* XEventTransition_create(void);
/**
 * @brief 创建指定事件源、事件类型和源状态的事件转换。
 * @param object 被监听对象，不转移所有权，可为 NULL。
 * @param type 需要匹配的事件类型。
 * @param sourceState 源状态，可为 NULL；非 NULL 时取得转换所有权。
 * @return 新事件转换；内存分配失败时返回 NULL。
 */
XEventTransition* XEventTransition_create_ex(XObject* object, XEventType type, XState* sourceState);
/**
 * @brief 初始化空事件转换。
 * @param transition 调用者提供的未初始化存储。
 */
void XEventTransition_init(XEventTransition* transition);
/**
 * @brief 初始化指定事件源、事件类型和源状态的事件转换。
 * @param transition 调用者提供的未初始化存储。
 * @param object 被监听对象，不转移所有权，可为 NULL。
 * @param type 需要匹配的事件类型。
 * @param sourceState 源状态，可为 NULL；非 NULL 时取得转换所有权。
 */
void XEventTransition_init_ex(XEventTransition* transition, XObject* object, XEventType type, XState* sourceState);

#define XEventTransition_delete_base XAbstractTransition_delete_base
#define XEventTransition_deinit_base XAbstractTransition_deinit_base

/**
 * @brief 获取事件源。
 * @param transition 事件转换。
 * @return 当前被监听对象；未设置或 transition 为 NULL 时返回 NULL。
 */
XObject* XEventTransition_eventSource(const XEventTransition* transition);
/**
 * @brief 设置事件源，源状态位于 configuration 时自动重新注册过滤器。
 * @param transition 事件转换。
 * @param object 新事件源，不转移所有权；NULL 表示取消监听。
 */
void XEventTransition_setEventSource(XEventTransition* transition, XObject* object);
/**
 * @brief 获取匹配的事件类型。
 * @param transition 事件转换。
 * @return 当前事件类型；transition 为 NULL 时返回 XEVENT_TYPE_NONE。
 */
XEventType XEventTransition_eventType(const XEventTransition* transition);
/**
 * @brief 设置匹配的事件类型。
 * @param transition 事件转换。
 * @param type 新事件类型；XEVENT_TYPE_NONE 表示只匹配 None 事件。
 */
void XEventTransition_setEventType(XEventTransition* transition, XEventType type);

#ifdef __cplusplus
}
#endif
#endif // XEVENTTRANSITION_H
