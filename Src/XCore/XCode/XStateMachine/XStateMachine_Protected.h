/**
 * @file       XStateMachine_Protected.h
 * @brief      XStateMachine 子类重载调度入口（protected/内部接口）。
 * @details     仅供子类和内部实现使用，不对外公开；普通用户 API 头文件链不得传递引入，
 *             使用者必须显式 include 本文件。
 */

#ifndef XSTATEMACHINE_PROTECTED_H
#define XSTATEMACHINE_PROTECTED_H

#include "XStateMachine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 调用转换选择开始钩子。
 * @param machine 正在选择转换的状态机。
 * @param event 当前待匹配事件。
 * @note 通过 EXStateMachine_BeginSelectTransitions 调度派生实现。
 */
void XStateMachine_beginSelectTransitions_base(XStateMachine* machine, XEvent* event);
/**
 * @brief 调用转换选择结束钩子。
 * @param machine 已完成转换选择的状态机。
 * @param event 当前待匹配事件。
 */
void XStateMachine_endSelectTransitions_base(XStateMachine* machine, XEvent* event);
/**
 * @brief 调用 microstep 开始钩子。
 * @param machine 即将执行 microstep 的状态机。
 * @param event 触发本次 microstep 的事件。
 */
void XStateMachine_beginMicrostep_base(XStateMachine* machine, XEvent* event);
/**
 * @brief 调用 microstep 结束钩子。
 * @param machine 已完成 microstep 的状态机。
 * @param event 触发本次 microstep 的事件。
 */
void XStateMachine_endMicrostep_base(XStateMachine* machine, XEvent* event);

#ifdef __cplusplus
}
#endif

#endif // XSTATEMACHINE_PROTECTED_H
