#include "XHistoryState.h"

#include "XAbstractTransition.h"
#include "XMemory.h"
#include "XStateMachine_p.h"

#include <stdlib.h>

static void VXHistoryState_deinit(XHistoryState* state)
{
    if (!state)
        return;

    state->m_defaultTransition = NULL;
    if (state->m_configuration) {
        XVector_delete_base((XClass*)state->m_configuration);
        state->m_configuration = NULL;
    }
    XVtableGetFunc(XAbstractState_class_init(), EXClass_Deinit,
                   void(*)(XAbstractState*))((XAbstractState*)state);
}

XVtable* XHistoryState_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XHistoryState))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XAbstractState);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHistoryState_deinit);
    return XVTABLE_DEFAULT;
}

XHistoryState* XHistoryState_create(void)
{
    return XHistoryState_create_ex(XHistoryState_ShallowHistory, NULL);
}

XHistoryState* XHistoryState_create_ex(XHistoryState_HistoryType type, XState* parent)
{
    XHistoryState* state = XNew(XHistoryState);
    if (!state)
        return NULL;
    XHistoryState_init_ex(state, type, parent);
    Set_Class_MemoryFree(state, XFree_System);
    return state;
}

void XHistoryState_init(XHistoryState* state)
{
    XHistoryState_init_ex(state, XHistoryState_ShallowHistory, NULL);
}

void XHistoryState_init_ex(XHistoryState* state, XHistoryState_HistoryType type, XState* parent)
{
    if (!state)
        return;
    XAbstractState_init((XAbstractState*)state, XAbstractState_HistoryState, parent);
    XClassSetVtable(state, XHistoryState);
    state->m_defaultTransition = NULL;
    state->m_historyType = type;
    state->m_configuration = XVector_Create(XAbstractState*);
}

XAbstractTransition* XHistoryState_defaultTransition(const XHistoryState* state)
{
    return state ? state->m_defaultTransition : NULL;
}

bool XHistoryState_setDefaultTransition(XHistoryState* state, XAbstractTransition* transition)
{
    if (!state)
        return false;
    if (state->m_defaultTransition == transition)
        return true;

    state->m_defaultTransition = transition;
    if (transition) {
        if (transition->m_sourceState)
            XState_removeTransition(transition->m_sourceState, transition);
        XObject_setParent((XObject*)transition, (XObject*)state);
    }
    XHistoryState_defaultTransitionChanged_signal(state);
    XHistoryState_defaultStateChanged_signal(state);
    return true;
}

XAbstractState* XHistoryState_defaultState(const XHistoryState* state)
{
    return state && state->m_defaultTransition
        ? XAbstractTransition_targetState(state->m_defaultTransition)
        : NULL;
}

bool XHistoryState_setDefaultState(XHistoryState* state, XAbstractState* defaultState)
{
    if (!state)
        return false;
    if (defaultState && defaultState->m_parentState != state->m_class.m_parentState)
        return false;
    if (XHistoryState_defaultState(state) == defaultState)
        return true;

    XAbstractTransition* transition = XNew(XAbstractTransition);
    if (!transition)
        return false;
    XAbstractTransition_init(transition, NULL);
    Set_Class_MemoryFree(transition, XFree_System);
    if (!XAbstractTransition_setTargetState(transition, defaultState)) {
        XAbstractTransition_delete_base((XClass*)transition);
        return false;
    }
    XHistoryState_setDefaultTransition(state, transition);
    return true;
}

XHistoryState_HistoryType XHistoryState_historyType(const XHistoryState* state)
{
    return state ? state->m_historyType : XHistoryState_ShallowHistory;
}

void XHistoryState_setHistoryType(XHistoryState* state, XHistoryState_HistoryType type)
{
    if (!state || state->m_historyType == type)
        return;
    state->m_historyType = type;
    XVector_clear_base((XContainer*)state->m_configuration);
    XHistoryState_historyTypeChanged_signal(state);
}

void* XHistoryState_defaultTransitionChanged_signal(XHistoryState* state)
{
    XEmitSignal((XObject*)state,
                XHistoryState_defaultTransitionChanged_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XHistoryState_defaultStateChanged_signal(XHistoryState* state)
{
    XEmitSignal((XObject*)state, XHistoryState_defaultStateChanged_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XHistoryState_historyTypeChanged_signal(XHistoryState* state)
{
    XEmitSignal((XObject*)state, XHistoryState_historyTypeChanged_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
