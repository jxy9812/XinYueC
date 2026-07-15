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

void XAbstractState_setActive_internal(XAbstractState* state, bool active);
void XAbstractState_setMachine_internal(XAbstractState* state, XStateMachine* machine);

void XStateMachine_registerTransition_internal(XStateMachine* machine, XAbstractTransition* transition);
void XStateMachine_unregisterTransition_internal(XStateMachine* machine, XAbstractTransition* transition);
void XStateMachine_registerSignalTransition_internal(XStateMachine* machine,
                                                      XSignalTransition* transition);
void XStateMachine_unregisterSignalTransition_internal(XStateMachine* machine,
                                                        XSignalTransition* transition);
void XStateMachine_removeTargetState_internal(XStateMachine* machine,
                                              XAbstractState* target);
void XStateMachine_postInternalEvent_internal(XStateMachine* machine, XEvent* event);
void XStateMachine_scheduleProcess_internal(XStateMachine* machine);
bool XStateMachine_isConfigured_internal(const XStateMachine* machine, const XAbstractState* state);
XEventType XStateMachine_signalEventType_internal(void);
XEventType XStateMachine_wrappedEventType_internal(void);

void XSignalTransition_register_internal(XSignalTransition* transition);
void XSignalTransition_unregister_internal(XSignalTransition* transition);
void XEventTransition_register_internal(XEventTransition* transition);
void XEventTransition_unregister_internal(XEventTransition* transition);

#endif // XSTATEMACHINE_P_H
