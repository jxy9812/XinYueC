#ifndef XSTATE_H
#define XSTATE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XAbstractState.h"
#include "XAbstractTransition.h"
XCLASS_DEFINE_BEGING(XState)
XCLASS_DEFINE_EXTEND_END(XState, XAbstractState);

// Qt 6.8: 子状态模式
typedef enum {
    XState_ExclusiveStates,  // 互斥子状态（默认）
    XState_ParallelStates,   // 并行子状态
} XState_ChildMode;

/**
 * @brief 基础状态类，可包含子状态和转换
 */
typedef struct XState
{
    XAbstractState m_class;         // 继承XAbstractState
    bool m_skipInitialState; // 临时跳过初始状态激活
    struct XAbstractState** m_childStates;// 子状态列表
    size_t m_childCount;             // 子状态数量
    size_t m_childCapacity;          // 子状态容量
    XAbstractTransition** m_transitions;  // 转换列表
    size_t m_transitionCount;        // 转换数量
    size_t m_transitionCapacity;     // 转换容量
    XAbstractState* m_initialState;  // 初始子状态
    // Qt 6.8: 子状态模式
    XState_ChildMode m_childMode;    // 子状态模式
    // Qt 6.8: 错误状态
    XAbstractState* m_errorState;    // 错误时进入的状态
} XState;
XVtable* XState_class_init();
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
#define XState_delete_base       XAbstractState_delete_base
#define XState_deinit_base       XAbstractState_deinit_base

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

bool XState_isChild(const XState* state, const XAbstractState* child);

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

// Qt 6.8: 子状态模式
XState_ChildMode XState_childMode(const XState* state);
void XState_setChildMode(XState* state, XState_ChildMode mode);

// Qt 6.8: 错误状态
XAbstractState* XState_errorState(const XState* state);
void XState_setErrorState(XState* state, XAbstractState* errorState);

/**
 * @brief 状态激活时调用
 * @param state 状态实例
 */
void XState_activate_base(XState* state);

/**
 * @brief 状态失活时调用
 * @param state 状态实例
 */
void XState_deactivate_base(XState* state);

// Qt 6.8: finished 信号 — 子最终状态被进入时发出
void* XState_finished_signal(XState* state);

#ifdef __cplusplus
}
#endif
#endif // XSTATE_H
