#include "XAbstractTransition.h"

#include "XMemory.h"
#include "XStateMachine_p.h"

#include <stdlib.h>

static bool VXAbstractTransition_eventTest(XAbstractTransition* transition, XEvent* event)
{
    (void)transition;
    return event && XEvent_type(event) == XEVENT_TYPE_NONE;
}

static void VXAbstractTransition_onTransition(XAbstractTransition* transition, XEvent* event)
{
    (void)transition;
    (void)event;
}

static void VXAbstractTransition_deinit(XAbstractTransition* transition)
{
    if (!transition)
        return;

    XStateMachine* machine = XAbstractTransition_machine(transition);
    if (machine)
        XStateMachine_unregisterTransition_internal(machine, transition);
    if (transition->m_sourceState)
        XState_removeTransition(transition->m_sourceState, transition);
    if (transition->m_targetStates) {
        XVector_delete_base((XClass*)transition->m_targetStates);
        transition->m_targetStates = NULL;
    }
    XVtableGetFunc(XObject_class_init(), EXClass_Deinit,
                   void(*)(XObject*))((XObject*)transition);
}

XVtable* XAbstractTransition_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractTransition)
	XCLASS_SET_CLASS_NAME_DEFAULT("XAbstractTransition");
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = { VXAbstractTransition_eventTest, VXAbstractTransition_onTransition };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractTransition_deinit);
    return XVTABLE_DEFAULT;
}

void XAbstractTransition_init(XAbstractTransition* transition, XState* sourceState)
{
    if (!transition)
        return;

    XObject_init((XObject*)transition);
    XClassSetVtable(transition, XAbstractTransition);
    transition->m_sourceState = NULL;
    transition->m_targetStates = XVector_Create(XAbstractState*);
    transition->m_transitionType = XAbstractTransition_ExternalTransition;
    transition->m_kind = XAbstractTransition_CustomTransition;

    if (sourceState)
        XState_addTransition(sourceState, transition);
}

XState* XAbstractTransition_sourceState(const XAbstractTransition* transition)
{
    return transition ? transition->m_sourceState : NULL;
}

XAbstractState* XAbstractTransition_targetState(const XAbstractTransition* transition)
{
    if (!transition || !transition->m_targetStates
        || XVector_isEmpty_base(
            (const XContainer*)transition->m_targetStates))
        return NULL;
    return XVector_At_Base(transition->m_targetStates, 0, XAbstractState*);
}

bool XAbstractTransition_setTargetState(XAbstractTransition* transition, XAbstractState* target)
{
    if (!transition || !transition->m_targetStates)
        return false;

    XAbstractState* previous = XAbstractTransition_targetState(transition);
    if (previous == target
        && XVector_size_base(
            (const XContainer*)transition->m_targetStates) <= 1)
        return true;

    XVector_clear_base((XContainer*)transition->m_targetStates);
    if (target && !XVector_push_back_1_base(transition->m_targetStates, &target))
        return false;

    XAbstractTransition_targetStateChanged_signal(transition);
    XAbstractTransition_targetStatesChanged_signal(transition);
    return true;
}

const XVector* XAbstractTransition_targetStates_const(const XAbstractTransition* transition)
{
    return transition ? transition->m_targetStates : NULL;
}

bool XAbstractTransition_setTargetStates(XAbstractTransition* transition, const XVector* targets)
{
    if (!transition || !transition->m_targetStates)
        return false;
    if (targets
        && XVector_typeSize_base((const XContainer*)targets)
            != sizeof(XAbstractState*))
        return false;

    if (targets) {
        for (int64_t i = 0;
             i < (int64_t)XVector_size_base(
                 (const XContainer*)targets); ++i) {
            if (!XVector_At_Base(targets, i, XAbstractState*))
                return false;
        }
    }

    XAbstractState* previous = XAbstractTransition_targetState(transition);
    XVector_clear_base((XContainer*)transition->m_targetStates);
    if (targets && !XVector_push_back_3(transition->m_targetStates, targets))
        return false;

    if (previous != XAbstractTransition_targetState(transition))
        XAbstractTransition_targetStateChanged_signal(transition);
    XAbstractTransition_targetStatesChanged_signal(transition);
    return true;
}

XAbstractTransition_TransitionType XAbstractTransition_transitionType(const XAbstractTransition* transition)
{
    return transition ? transition->m_transitionType : XAbstractTransition_ExternalTransition;
}

void XAbstractTransition_setTransitionType(XAbstractTransition* transition, XAbstractTransition_TransitionType type)
{
    if (transition)
        transition->m_transitionType = type;
}

XStateMachine* XAbstractTransition_machine(const XAbstractTransition* transition)
{
    return transition && transition->m_sourceState
        ? XAbstractState_machine((XAbstractState*)transition->m_sourceState)
        : NULL;
}

bool XAbstractTransition_eventTest_base(XAbstractTransition* transition, XEvent* event)
{
    if (ISNULL(transition, "XAbstractTransition") || ISNULL(XClassGetVtable(transition), "Vtable"))
        return false;
    return XClassGetVirtualFunc(transition, EXAbstractTransition_EventTest,
                                bool(*)(XAbstractTransition*, XEvent*))(transition, event);
}

void XAbstractTransition_onTransition_base(XAbstractTransition* transition, XEvent* event)
{
    if (ISNULL(transition, "XAbstractTransition") || ISNULL(XClassGetVtable(transition), "Vtable"))
        return;
    XClassGetVirtualFunc(transition, EXAbstractTransition_OnTransition,
                         void(*)(XAbstractTransition*, XEvent*))(transition, event);
}

void* XAbstractTransition_triggered_signal(XAbstractTransition* transition)
{
    XEmitSignal((XObject*)transition, XAbstractTransition_triggered_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XAbstractTransition_targetStateChanged_signal(XAbstractTransition* transition)
{
    XEmitSignal((XObject*)transition,
                XAbstractTransition_targetStateChanged_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XAbstractTransition_targetStatesChanged_signal(XAbstractTransition* transition)
{
    XEmitSignal((XObject*)transition,
                XAbstractTransition_targetStatesChanged_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
