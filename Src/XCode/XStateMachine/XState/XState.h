#ifndef XSTATE_H
#define XSTATE_H

#include "XAbstractState.h"
#include "XAbstractTransition.h"

/**
 * @brief 基础状态类，可包含子状态和转换
 */
typedef struct XState {
    XAbstractState parent;         // 继承XAbstractState
    XAbstractTransition** transitions;  // 转换列表
    size_t transitionCount;        // 转换数量
    size_t transitionCapacity;     // 转换容量
    XAbstractState* initialState;  // 初始子状态
} XState;

/**
 * @brief 创建状态实例
 * @return 新创建的状态实例，失败返回NULL
 */
XState* XState_create();

/**
 * @brief 初始化状态
 * @param state 状态实例
 */
void XState_init(XState* state);

/**
 * @brief 销毁状态
 * @param state 状态实例
 */
void XState_destroy(XState* state);

/**
 * @brief 添加子状态
 * @param state 父状态
 * @param child 要添加的子状态
 * @return 成功返回true，失败返回false
 */
bool XState_addState(XState* state, XAbstractState* child);

/**
 * @brief 移除子状态
 * @param state 父状态
 * @param child 要移除的子状态
 * @return 成功返回true，失败返回false
 */
bool XState_removeState(XState* state, XAbstractState* child);

/**
 * @brief 获取子状态数量
 * @param state 状态实例
 * @return 子状态数量
 */
size_t XState_childCount(const XState* state);

/**
 * @brief 获取指定索引的子状态
 * @param state 状态实例
 * @param index 索引
 * @return 子状态指针，索引无效返回NULL
 */
XAbstractState* XState_child(const XState* state, size_t index);

/**
 * @brief 添加转换
 * @param state 源状态
 * @param transition 要添加的转换
 * @return 成功返回true，失败返回false
 */
bool XState_addTransition(XState* state, XAbstractTransition* transition);

/**
 * @brief 移除转换
 * @param state 源状态
 * @param transition 要移除的转换
 * @return 成功返回true，失败返回false
 */
bool XState_removeTransition(XState* state, XAbstractTransition* transition);

/**
 * @brief 获取转换数量
 * @param state 状态实例
 * @return 转换数量
 */
size_t XState_transitionCount(const XState* state);

/**
 * @brief 获取指定索引的转换
 * @param state 状态实例
 * @param index 索引
 * @return 转换指针，索引无效返回NULL
 */
XAbstractTransition* XState_transition(const XState* state, size_t index);

/**
 * @brief 设置初始子状态
 * @param state 父状态
 * @param initialState 初始子状态
 */
void XState_setInitialState(XState* state, XAbstractState* initialState);

/**
 * @brief 获取初始子状态
 * @param state 状态实例
 * @return 初始子状态指针
 */
XAbstractState* XState_initialState(const XState* state);

/**
 * @brief 状态激活时调用
 * @param state 状态实例
 * @param machine 所属状态机
 */
void XState_activate(XState* state, XStateMachine* machine);

/**
 * @brief 状态失活时调用
 * @param state 状态实例
 * @param machine 所属状态机
 */
void XState_deactivate(XState* state, XStateMachine* machine);

#endif // XSTATE_H