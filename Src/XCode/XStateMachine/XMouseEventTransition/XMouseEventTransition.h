#ifndef XMOUSEEVENTTRANSITION_H
#define XMOUSEEVENTTRANSITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XEventTransition.h"

XCLASS_DEFINE_BEGING(XMouseEventTransition)
XCLASS_DEFINE_EXTEND_END(XMouseEventTransition, XEventTransition);

/**
 * @brief 由指定对象的鼠标事件触发的转换，对应 Qt 6.8 的 QMouseEventTransition。
 * @note 非空 hit-test 路径使用 XVector<XPoint> 表示闭合多边形。
 */
typedef struct XMouseEventTransition {
    XEventTransition m_class;          ///< 继承 XEventTransition。
    XMouseButton m_button;             ///< 需要匹配的鼠标按键。
    XKeyboardModifiers m_modifierMask; ///< 必须存在的修饰键位掩码。
    XVector* m_hitTestPath;             ///< 局部坐标多边形路径，元素类型为 XPoint。
} XMouseEventTransition;

/**
 * @brief 初始化 XMouseEventTransition 虚函数表。
 * @return XMouseEventTransition 的共享虚函数表。
 */
XVtable* XMouseEventTransition_class_init(void);
/**
 * @brief 创建未指定事件源的鼠标事件转换。
 * @return 新鼠标事件转换；内存分配失败时返回 NULL。
 */
XMouseEventTransition* XMouseEventTransition_create(void);
/**
 * @brief 创建指定事件源、事件类型、按键和源状态的鼠标事件转换。
 * @param object 被监听对象，不转移所有权，可为 NULL。
 * @param type 鼠标按下、释放、双击或移动事件类型。
 * @param button 需要匹配的鼠标按键。
 * @param sourceState 源状态，可为 NULL；非 NULL 时取得转换所有权。
 * @return 新鼠标事件转换；内存分配失败时返回 NULL。
 */
XMouseEventTransition* XMouseEventTransition_create_ex(XObject* object, XEventType type,
                                                       XMouseButton button,
                                                       XState* sourceState);
/**
 * @brief 初始化空鼠标事件转换。
 * @param transition 调用者提供的未初始化存储。
 */
void XMouseEventTransition_init(XMouseEventTransition* transition);
/**
 * @brief 初始化指定事件源、事件类型、按键和源状态的鼠标事件转换。
 * @param transition 调用者提供的未初始化存储。
 * @param object 被监听对象，不转移所有权，可为 NULL。
 * @param type 鼠标事件类型。
 * @param button 需要匹配的鼠标按键。
 * @param sourceState 源状态，可为 NULL；非 NULL 时取得转换所有权。
 */
void XMouseEventTransition_init_ex(XMouseEventTransition* transition, XObject* object,
                                   XEventType type, XMouseButton button,
                                   XState* sourceState);

#define XMouseEventTransition_delete_base XEventTransition_delete_base
#define XMouseEventTransition_deinit_base XEventTransition_deinit_base

/**
 * @brief 获取需要匹配的鼠标按键。
 * @param transition 鼠标事件转换。
 * @return 当前鼠标按键；transition 为 NULL 时返回 NoButton。
 */
XMouseButton XMouseEventTransition_button(const XMouseEventTransition* transition);
/**
 * @brief 设置需要匹配的鼠标按键。
 * @param transition 鼠标事件转换。
 * @param button 新鼠标按键。
 */
void XMouseEventTransition_setButton(XMouseEventTransition* transition, XMouseButton button);
/**
 * @brief 获取必须存在的修饰键位掩码。
 * @param transition 鼠标事件转换。
 * @return 当前修饰键掩码；transition 为 NULL 时返回 NoModifier。
 */
XKeyboardModifiers XMouseEventTransition_modifierMask(const XMouseEventTransition* transition);
/**
 * @brief 设置必须存在的修饰键位掩码。
 * @param transition 鼠标事件转换。
 * @param modifiers 必须同时存在的修饰键组合。
 */
void XMouseEventTransition_setModifierMask(XMouseEventTransition* transition,
                                           XKeyboardModifiers modifiers);
/**
 * @brief 获取 hit-test 多边形路径的只读内部引用。
 * @param transition 鼠标事件转换。
 * @return 元素类型为 XPoint 的内部 XVector；transition 为 NULL 时返回 NULL。
 * @warning 调用者不得修改或释放返回容器。
 */
const XVector* XMouseEventTransition_hitTestPath_const(const XMouseEventTransition* transition);
/**
 * @brief 拷贝设置 hit-test 多边形路径。
 * @param transition 鼠标事件转换。
 * @param path 元素类型为 XPoint；NULL 表示清空位置限制。
 * @return 路径类型有效且拷贝成功时返回 true。
 * @note 少于三个点的非空路径不包含任何位置；空路径匹配任意位置。
 */
bool XMouseEventTransition_setHitTestPath(XMouseEventTransition* transition,
                                          const XVector* path);

#ifdef __cplusplus
}
#endif
#endif // XMOUSEEVENTTRANSITION_H
