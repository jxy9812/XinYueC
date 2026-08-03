#include "XState.h"

#include "XMemory.h"
#include "XSignalTransition.h"
#include "XStateMachine_p.h"

#include <stdlib.h>

static void VXState_deinit(XState* state)
{
    if (!state)
        return;

    if (state->m_transitions) {
        for (int64_t i = 0;
             i < (int64_t)XVector_size_base(
                 (const XContainer*)state->m_transitions); ++i) {
            XAbstractTransition* transition = XVector_At_Base(state->m_transitions, i, XAbstractTransition*);
            XStateMachine_unregisterTransition_internal(state->m_class.m_machine, transition);
            transition->m_sourceState = NULL;
        }
        XVector_delete_base((XClass*)state->m_transitions);
        state->m_transitions = NULL;
    }

    if (state->m_childStates) {
        for (int64_t i = 0;
             i < (int64_t)XVector_size_base(
                 (const XContainer*)state->m_childStates); ++i) {
            XAbstractState* child = XVector_At_Base(state->m_childStates, i, XAbstractState*);
            child->m_parentState = NULL;
            XAbstractState_setMachine_internal(child, NULL);
        }
        XVector_delete_base((XClass*)state->m_childStates);
        state->m_childStates = NULL;
    }

    state->m_initialState = NULL;
    state->m_errorState = NULL;
    XVtableGetFunc(XAbstractState_class_init(), EXClass_Deinit,
                   void(*)(XAbstractState*))((XAbstractState*)state);
}

XVtable* XState_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XState)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XAbstractState);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXState_deinit);
    return XVTABLE_DEFAULT;
}

XState* XState_create(void)
{
    return XState_create_ex(XState_ExclusiveStates, NULL);
}

XState* XState_create_ex(XState_ChildMode childMode, XState* parent)
{
    XState* state = XNew(XState);
    if (!state)
        return NULL;
    XState_init_ex(state, childMode, parent);
    Set_Class_MemoryFree(state, XFree_System);
    return state;
}

void XState_init(XState* state)
{
    XState_init_ex(state, XState_ExclusiveStates, NULL);
}

void XState_init_ex(XState* state, XState_ChildMode childMode, XState* parent)
{
    if (!state)
        return;

    XAbstractState_init((XAbstractState*)state, XAbstractState_StandardState, NULL);
    XClassSetVtable(state, XState);
    state->m_childStates = XVector_Create(XAbstractState*);
    state->m_transitions = XVector_Create(XAbstractTransition*);
    state->m_initialState = NULL;
    state->m_errorState = NULL;
    state->m_childMode = childMode;
    if (parent)
        XState_addChild_internal(parent, (XAbstractState*)state);
}

bool XState_addChild_internal(XState* parent, XAbstractState* child)
{
    if (!parent || !child || !parent->m_childStates || (XAbstractState*)parent == child)
        return false;

    for (XState* ancestor = parent; ancestor; ancestor = ancestor->m_class.m_parentState) {
        if ((XAbstractState*)ancestor == child)
            return false;
    }

    if (child->m_parentState == parent
        && XVector_indexOf(parent->m_childStates, &child, 0) >= 0) {
        return true;
    }

    if (child->m_parentState)
        XState_removeChild_internal(child->m_parentState, child);

    if (!XVector_push_back_1_base(parent->m_childStates, &child))
        return false;

    child->m_parentState = parent;
    XStateMachine* machine = parent->m_class.m_kind == XAbstractState_StateMachine
        ? (XStateMachine*)parent
        : parent->m_class.m_machine;
    XAbstractState_setMachine_internal(child, machine);
    XObject_setParent((XObject*)child, (XObject*)parent);
    return true;
}

void XState_removeChild_internal(XState* parent, XAbstractState* child)
{
    if (!parent || !child || !parent->m_childStates)
        return;

    int64_t index = XVector_indexOf(parent->m_childStates, &child, 0);
    if (index < 0)
        return;

    XVector_removeAt_base(parent->m_childStates, index);
    if (parent->m_initialState == child) {
        parent->m_initialState = NULL;
        XState_initialStateChanged_signal(parent);
    }
    if (parent->m_errorState == child) {
        parent->m_errorState = NULL;
        XState_errorStateChanged_signal(parent);
    }
    child->m_parentState = NULL;
    XAbstractState_setMachine_internal(child, NULL);
    if (XObject_parent((XObject*)child) == (XObject*)parent)
        XObject_setParent((XObject*)child, NULL);
}

const XVector* XState_childStates_const_internal(const XState* state)
{
    return state ? state->m_childStates : NULL;
}

XAbstractState* XState_errorState(const XState* state)
{
    return state ? state->m_errorState : NULL;
}

bool XState_setErrorState(XState* state, XAbstractState* errorState)
{
    if (!state)
        return false;
    if (errorState && errorState->m_kind == XAbstractState_StateMachine)
        return false;
    if (errorState && state->m_class.m_machine && errorState->m_machine
        && state->m_class.m_machine != errorState->m_machine) {
        return false;
    }
    if (state->m_errorState == errorState)
        return true;

    state->m_errorState = errorState;
    XState_errorStateChanged_signal(state);
    return true;
}

bool XState_addTransition(XState* state, XAbstractTransition* transition)
{
    if (!state || !transition || !state->m_transitions)
        return false;
    if (transition->m_sourceState == state
        && XVector_indexOf(state->m_transitions, &transition, 0) >= 0) {
        return true;
    }

    const XVector* targets = XAbstractTransition_targetStates_const(transition);
    for (int64_t i = 0;
         targets
             && i < (int64_t)XVector_size_base(
                 (const XContainer*)targets); ++i) {
        XAbstractState* target = XVector_At_Base(targets, i, XAbstractState*);
        if (!target)
            return false;
        if (state->m_class.m_machine && target->m_machine
            && state->m_class.m_machine != target->m_machine) {
            return false;
        }
    }

    if (transition->m_sourceState)
        XState_removeTransition(transition->m_sourceState, transition);
    if (!XVector_push_back_1_base(state->m_transitions, &transition))
        return false;

    transition->m_sourceState = state;
    XObject_setParent((XObject*)transition, (XObject*)state);
    if (state->m_class.m_machine
        && XStateMachine_isConfigured_internal(
            state->m_class.m_machine, (XAbstractState*)state)) {
        XStateMachine_registerTransition_internal(state->m_class.m_machine, transition);
    }
    return true;
}

XSignalTransition* XState_addTransition_2(XState* state, const XObject* sender,
                                          size_t signal, XAbstractState* target)
{
    if (!state || !sender || !signal || !target)
        return NULL;

    XSignalTransition* transition = XSignalTransition_create_ex(sender, signal, state);
    if (!transition)
        return NULL;
    if (!XAbstractTransition_setTargetState((XAbstractTransition*)transition, target)) {
        XSignalTransition_delete_base((XClass*)transition);
        return NULL;
    }
    return transition;
}

XAbstractTransition* XState_addTransition_3(XState* state, XAbstractState* target)
{
    if (!state || !target)
        return NULL;

    XAbstractTransition* transition = XNew(XAbstractTransition);
    if (!transition)
        return NULL;
    XAbstractTransition_init(transition, state);
    Set_Class_MemoryFree(transition, XFree_System);
    if (!XAbstractTransition_setTargetState(transition, target)) {
        XAbstractTransition_delete_base((XClass*)transition);
        return NULL;
    }
    return transition;
}

bool XState_removeTransition(XState* state, XAbstractTransition* transition)
{
    if (!state || !transition || !state->m_transitions)
        return false;

    int64_t index = XVector_indexOf(state->m_transitions, &transition, 0);
    if (index < 0 || transition->m_sourceState != state)
        return false;

    if (state->m_class.m_machine)
        XStateMachine_unregisterTransition_internal(state->m_class.m_machine, transition);
    XVector_removeAt_base(state->m_transitions, index);
    transition->m_sourceState = NULL;
    if (XObject_parent((XObject*)transition) == (XObject*)state)
        XObject_setParent((XObject*)transition, NULL);
    return true;
}

const XVector* XState_transitions_const(const XState* state)
{
    return state ? state->m_transitions : NULL;
}

XAbstractState* XState_initialState(const XState* state)
{
    return state ? state->m_initialState : NULL;
}

bool XState_setInitialState(XState* state, XAbstractState* initialState)
{
    if (!state || state->m_childMode == XState_ParallelStates)
        return false;
    if (initialState && initialState->m_parentState != state)
        return false;
    if (state->m_initialState == initialState)
        return true;

    state->m_initialState = initialState;
    XState_initialStateChanged_signal(state);
    return true;
}

XState_ChildMode XState_childMode(const XState* state)
{
    return state ? state->m_childMode : XState_ExclusiveStates;
}

void XState_setChildMode(XState* state, XState_ChildMode mode)
{
    if (!state || state->m_childMode == mode)
        return;
    if (mode == XState_ParallelStates && state->m_initialState) {
        state->m_initialState = NULL;
        XState_initialStateChanged_signal(state);
    }
    state->m_childMode = mode;
    XState_childModeChanged_signal(state);
}

void* XState_finished_signal(XState* state)
{
    XEmitSignal((XObject*)state, XState_finished_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XState_propertiesAssigned_signal(XState* state)
{
    XEmitSignal((XObject*)state, XState_propertiesAssigned_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XState_childModeChanged_signal(XState* state)
{
    XEmitSignal((XObject*)state, XState_childModeChanged_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XState_initialStateChanged_signal(XState* state)
{
    XEmitSignal((XObject*)state, XState_initialStateChanged_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XState_errorStateChanged_signal(XState* state)
{
    XEmitSignal((XObject*)state, XState_errorStateChanged_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
