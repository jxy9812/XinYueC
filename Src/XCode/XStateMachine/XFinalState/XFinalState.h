#ifndef XFINALSTATE_H
#define XFINALSTATE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XAbstractState.h"
XCLASS_DEFINE_BEGING(XFinalState)
XCLASS_DEFINE_EXTEND_END(XFinalState, XAbstractState);
/**
 * @brief 最终状态类，表示状态机中的终止状态
 */
typedef struct XFinalState {
    XAbstractState m_class;  // 继承XAbstractState
} XFinalState;
XVtable* XFinalState_class_init();
/**
 * @brief 创建最终状态实例
 * @return 新创建的最终状态实例，失败返回NULL
 */
XFinalState* XFinalState_create();

/**
 * @brief 初始化最终状态
 * @param state 最终状态实例
 */
void XFinalState_init(XFinalState* state);

/**
 * @brief 销毁最终状态
 * @param state 最终状态实例
 */
#define XFinalState_delete_base       XAbstractState_delete_base
#define XFinalState_deinit_base       XAbstractState_deinit_base

/**
 * @brief 激活最终状态
 * @param state 最终状态实例
 * @param m_machine 所属状态机
 */
void XFinalState_activate(XFinalState* state);
#ifdef __cplusplus
}
#endif
#endif // XFINALSTATE_H