#include "XAbstractState.h"

#include "XMemory.h"
#include "XStateMachine_p.h"

#include <stdlib.h>

static void VXAbstractState_onEntry(XAbstractState* state, XEvent* event)
{
    (void)state;
    (void)event;
}

static void VXAbstractState_onExit(XAbstractState* state, XEvent* event)
{
    (void)state;
    (void)event;
}

static void VXAbstractState_deinit(XAbstractState* state)
{
    if (!state)
        return;

    if (state->m_machine)
        XStateMachine_removeTargetState_internal(state->m_machine, state);
    if (state->m_parentState)
        XState_removeChild_internal(state->m_parentState, state);

    state->m_parentState = NULL;
    state->m_machine = NULL;
    state->m_active = false;
    XVtableGetFunc(XObject_class_init(), EXClass_Deinit,
                   void(*)(XObject*))((XObject*)state);
}

XVtable* XAbstractState_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractState)
	XCLASS_SET_CLASS_NAME_DEFAULT("XAbstractState");
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = { VXAbstractState_onEntry, VXAbstractState_onExit };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractState_deinit);
    return XVTABLE_DEFAULT;
}

void XAbstractState_init(XAbstractState* state, XAbstractState_Kind kind, XState* parent)
{
    if (!state)
        return;

    XObject_init((XObject*)state);
    XClassSetVtable(state, XAbstractState);
    state->m_kind = kind;
    state->m_parentState = NULL;
    state->m_machine = NULL;
    state->m_active = false;

    if (parent)
        XState_addChild_internal(parent, state);
}

XState* XAbstractState_parentState(const XAbstractState* state)
{
    return state ? state->m_parentState : NULL;
}

XStateMachine* XAbstractState_machine(const XAbstractState* state)
{
    return state ? state->m_machine : NULL;
}

bool XAbstractState_active(const XAbstractState* state)
{
    return state ? state->m_active : false;
}

void XAbstractState_onEntry_base(XAbstractState* state, XEvent* event)
{
    if (ISNULL(state, "XAbstractState") || ISNULL(XClassGetVtable(state), "Vtable"))
        return;
    XClassGetVirtualFunc(state, EXAbstractState_OnEntry, void(*)(XAbstractState*, XEvent*))(state, event);
}

void XAbstractState_onExit_base(XAbstractState* state, XEvent* event)
{
    if (ISNULL(state, "XAbstractState") || ISNULL(XClassGetVtable(state), "Vtable"))
        return;
    XClassGetVirtualFunc(state, EXAbstractState_OnExit, void(*)(XAbstractState*, XEvent*))(state, event);
}

void XAbstractState_setActive_internal(XAbstractState* state, bool active)
{
    if (!state || state->m_active == active)
        return;
    state->m_active = active;
    XAbstractState_activeChanged_signal(state, active);
}

void XAbstractState_setMachine_internal(XAbstractState* state, XStateMachine* machine)
{
    if (!state || state->m_machine == machine)
        return;

    state->m_machine = machine;
    if (state->m_kind != XAbstractState_StandardState && state->m_kind != XAbstractState_StateMachine)
        return;

    XState* group = (XState*)state;
    const XVector* children = XState_childStates_const_internal(group);
    if (!children)
        return;

    for (int64_t i = 0;
         i < (int64_t)XVector_size_base((const XContainer*)children); ++i) {
        XAbstractState* child = XVector_At_Base(children, i, XAbstractState*);
        XAbstractState_setMachine_internal(child, machine);
    }
}

void* XAbstractState_entered_signal(XAbstractState* state)
{
    XEmitSignal((XObject*)state, XAbstractState_entered_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XAbstractState_exited_signal(XAbstractState* state)
{
    XEmitSignal((XObject*)state, XAbstractState_exited_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XAbstractState_activeChanged_signal(XAbstractState* state, bool active)
{
    XEmitSignal((XObject*)state, XAbstractState_activeChanged_signal,
                XVarList_create(2, sizeof(bool), &active),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
