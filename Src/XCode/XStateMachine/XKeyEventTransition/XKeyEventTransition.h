#ifndef XKEYEVENTTRANSITION_H
#define XKEYEVENTTRANSITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XEventTransition.h"

XCLASS_DEFINE_BEGING(XKeyEventTransition)
XCLASS_DEFINE_EXTEND_END(XKeyEventTransition, XEventTransition);

/**
 * @brief 由指定对象的键盘事件触发的转换，对应 Qt 6.8 的 QKeyEventTransition。
 * @note 修饰键按掩码匹配，事件可以包含匹配掩码之外的其他修饰键。
 */
typedef struct XKeyEventTransition {
    XEventTransition m_class;          ///< 继承 XEventTransition。
    int m_key;                         ///< 需要匹配的按键码。
    XKeyboardModifiers m_modifierMask; ///< 必须存在的修饰键位掩码。
} XKeyEventTransition;

/**
 * @brief 初始化 XKeyEventTransition 虚函数表。
 * @return XKeyEventTransition 的共享虚函数表。
 */
XVtable* XKeyEventTransition_class_init(void);
/**
 * @brief 创建未指定事件源的键盘事件转换。
 * @return 新键盘事件转换；内存分配失败时返回 NULL。
 */
/**
 * @brief 创建指定事件源、事件类型、按键和源状态的键盘事件转换。
 * @param object 被监听对象，不转移所有权，可为 NULL。
 * @param type 键盘事件类型，通常为 KEY_PRESS 或 KEY_RELEASE。
 * @param key 需要匹配的按键码。
 * @param sourceState 源状态，可为 NULL；非 NULL 时取得转换所有权。
 * @return 新键盘事件转换；内存分配失败时返回 NULL。
 */
XKeyEventTransition* XKeyEventTransition_create_ex(XMemoryType memory, XObject* object, XEventType type,
                                                   int key, XState* sourceState);
/**
 * @brief 初始化空键盘事件转换。
 * @param transition 调用者提供的未初始化存储。
 */
void XKeyEventTransition_init(XKeyEventTransition* transition);
/**
 * @brief 初始化指定事件源、事件类型、按键和源状态的键盘事件转换。
 * @param transition 调用者提供的未初始化存储。
 * @param object 被监听对象，不转移所有权，可为 NULL。
 * @param type 键盘事件类型。
 * @param key 需要匹配的按键码。
 * @param sourceState 源状态，可为 NULL；非 NULL 时取得转换所有权。
 */
void XKeyEventTransition_init_ex(XKeyEventTransition* transition, XObject* object,
                                 XEventType type, int key, XState* sourceState);

#define XKeyEventTransition_delete_base XEventTransition_delete_base
#define XKeyEventTransition_deinit_base XEventTransition_deinit_base

/**
 * @brief 获取需要匹配的按键码。
 * @param transition 键盘事件转换。
 * @return 当前按键码；transition 为 NULL 时返回 0。
 */
int XKeyEventTransition_key(const XKeyEventTransition* transition);
/**
 * @brief 设置需要匹配的按键码。
 * @param transition 键盘事件转换。
 * @param key 新按键码。
 */
void XKeyEventTransition_setKey(XKeyEventTransition* transition, int key);
/**
 * @brief 获取必须存在的修饰键位掩码。
 * @param transition 键盘事件转换。
 * @return 当前修饰键掩码；transition 为 NULL 时返回 NoModifier。
 */
XKeyboardModifiers XKeyEventTransition_modifierMask(const XKeyEventTransition* transition);
/**
 * @brief 设置必须存在的修饰键位掩码。
 * @param transition 键盘事件转换。
 * @param modifiers 必须同时存在的修饰键组合。
 * @note 事件包含额外修饰键时仍可匹配。
 */
void XKeyEventTransition_setModifierMask(XKeyEventTransition* transition,
                                         XKeyboardModifiers modifiers);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XKeyEventTransition_create
#define XKeyEventTransition_create() \
	XKeyEventTransition_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, XEVENT_TYPE_NONE, 0, NULL)

#endif // XKEYEVENTTRANSITION_H
