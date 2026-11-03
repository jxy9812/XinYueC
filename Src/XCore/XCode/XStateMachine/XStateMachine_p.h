#ifndef XSTATEMACHINE_P_H
#define XSTATEMACHINE_P_H

#include "XStateMachine.h"
#include "XEventTransition.h"
#include "XFinalState.h"
#include "XHistoryState.h"
#include "XSignalTransition.h"

bool XState_addChild_internal(XState* parent, XAbstractState* child);
void XState_removeChild_internal(XState* parent, XAbstractState* child);
const XVector* XState_childStates_const_internal(const XState* state);
/* 登记/注销一条恢复记录（对标 QStateMachinePrivate::registerRestorable/unregisterRestorables）。 */
void XStateMachine_registerRestorable_internal(XStateMachine* machine, XAbstractState* state,
                                               XObject* object, const char* propertyName,
                                               const XVariant* value);
void XStateMachine_unregisterRestorables_internal(XStateMachine* machine, const XVector* states,
                                                  XObject* object, const char* propertyName);
/* 计算退出状态集合的待恢复记录（对标 computePendingRestorables）。元素类型为
 * XStateMachine_Restorable，由调用方用 XStateMachine_restorablesRelease_internal 释放。 */
XVector* XStateMachine_computePendingRestorables_internal(const XStateMachine* machine,
                                                          const XVector* statesToExit);
/* 清空指定状态的全部恢复记录（状态被销毁时调用）。 */
void XStateMachine_dropRestorablesForState_internal(XStateMachine* machine, XAbstractState* state);
/* 释放 computePendingRestorables_internal 返回的容器及其元素。 */
void XStateMachine_restorablesRelease_internal(XVector* restorables);

void XAbstractState_setActive_internal(XAbstractState* state, bool active);
void XAbstractState_setMachine_internal(XAbstractState* state, XStateMachine* machine);

void XStateMachine_registerTransition_internal(XStateMachine* machine, XAbstractTransition* transition);
/* deferred: true=运行期延迟回收,false=析构路径同步删(参照 XActionGroup 双模式先例) */
void XStateMachine_unregisterTransition_internal(XStateMachine* machine, XAbstractTransition* transition, bool deferred);
void XStateMachine_registerSignalTransition_internal(XStateMachine* machine,
                                                      XSignalTransition* transition);
/* deferred: true=运行期延迟回收,false=析构路径同步删(参照 XActionGroup 双模式先例) */
void XStateMachine_unregisterSignalTransition_internal(XStateMachine* machine,
                                                        XSignalTransition* transition,
                                                        bool deferred);
void XStateMachine_removeTargetState_internal(XStateMachine* machine,
                                              XAbstractState* target);
void XStateMachine_postInternalEvent_internal(XStateMachine* machine, XEvent* event);
void XStateMachine_scheduleProcess_internal(XStateMachine* machine);
bool XStateMachine_isConfigured_internal(const XStateMachine* machine, const XAbstractState* state);
XEventType XStateMachine_signalEventType_internal(void);
XEventType XStateMachine_wrappedEventType_internal(void);
/* 清除状态的历史记录（仅供状态机启动/历史类型变更使用）。 */
void XStateMachine_clearHistory_internal(XStateMachine* machine);

void XSignalTransition_register_internal(XSignalTransition* transition);
/* deferred: true=运行期延迟回收,false=析构路径同步删(参照 XActionGroup 双模式先例) */
void XSignalTransition_unregister_internal(XSignalTransition* transition, bool deferred);
void XEventTransition_register_internal(XEventTransition* transition);
void XEventTransition_unregister_internal(XEventTransition* transition);

#endif // XSTATEMACHINE_P_H
