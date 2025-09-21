#ifndef XFINALSTATE_H
#define XFINALSTATE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XAbstractState.h"

/**
 * @brief 最终状态类，表示状态机中的终止状态
 */
typedef struct XFinalState {
    XAbstractState m_class;  // 继承XAbstractState
} XFinalState;

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
void XFinalState_destroy(XFinalState* state);

/**
 * @brief 激活最终状态
 * @param state 最终状态实例
 * @param m_machine 所属状态机
 */
void XFinalState_activate(XFinalState* state, XStateMachine* machine);
#ifdef __cplusplus
}
#endif
#endif // XFINALSTATE_H