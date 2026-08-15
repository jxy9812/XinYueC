#ifndef XFINALSTATE_H
#define XFINALSTATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XAbstractState.h"

XCLASS_DEFINE_BEGING(XFinalState)
XCLASS_DEFINE_EXTEND_END(XFinalState, XAbstractState);

/**
 * @brief 最终状态，对应 Qt 6.8 的 QFinalState。
 * @note 最终状态自身不停止状态机；完成语义由 XStateMachine 统一处理。
 */
typedef struct XFinalState {
    XAbstractState m_class; ///< 继承 XAbstractState。
} XFinalState;

/**
 * @brief 初始化 XFinalState 虚函数表。
 * @return XFinalState 的共享虚函数表。
 */
XVtable* XFinalState_class_init(void);
/**
 * @brief 创建无父状态的最终状态。
 * @return 新最终状态；内存分配失败时返回 NULL。
 */
/**
 * @brief 创建指定父状态的最终状态。
 * @param parent 父状态，可为 NULL；非 NULL 时取得最终状态所有权。
 * @return 新最终状态；内存分配失败时返回 NULL。
 */
XFinalState* XFinalState_create_ex(XMemoryType memory, XState* parent);
/**
 * @brief 初始化无父状态的最终状态。
 * @param state 调用者提供的未初始化存储。
 */
void XFinalState_init(XFinalState* state);
/**
 * @brief 初始化指定父状态的最终状态。
 * @param state 调用者提供的未初始化存储。
 * @param parent 父状态，可为 NULL；非 NULL 时取得最终状态所有权。
 */
void XFinalState_init_ex(XFinalState* state, XState* parent);

#define XFinalState_delete_base XAbstractState_delete_base
#define XFinalState_deinit_base XAbstractState_deinit_base

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XFinalState_create
#define XFinalState_create() \
	XFinalState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL)

#endif // XFINALSTATE_H
