#include "XEventTransition.h"

#include "XMemory.h"
#include "XStateMachine_p.h"

#include <stdlib.h>

static bool VXEventTransition_eventTest(XEventTransition* transition, XEvent* event)
{
    if (!transition || !event || XEvent_type(event) != XStateMachine_wrappedEventType_internal())
        return false;

    XStateMachine_WrappedEvent* wrapped = (XStateMachine_WrappedEvent*)event;
    return wrapped->m_object == transition->m_eventSource
        && wrapped->m_event
        && XEvent_type(wrapped->m_event) == transition->m_eventType;
}

static void VXEventTransition_onTransition(XEventTransition* transition, XEvent* event)
{
    (void)transition;
    (void)event;
}

static void VXEventTransition_deinit(XEventTransition* transition)
{
    if (!transition)
        return;
    XEventTransition_unregister_internal(transition);
    transition->m_eventSource = NULL;
    XVtableGetFunc(XAbstractTransition_class_init(), EXClass_Deinit,
                   void(*)(XAbstractTransition*))(
                       (XAbstractTransition*)transition);
}

static bool XEventTransition_sourceIsRegistered(const XState* state,
                                                const XEventTransition* ignored,
                                                const XObject* source)
{
    if (!state || !source)
        return false;
    const XVector* transitions = XState_transitions_const(state);
    for (int64_t i = 0;
         transitions
             && i < (int64_t)XVector_size_base(
                 (const XContainer*)transitions); ++i) {
        XAbstractTransition* candidate = XVector_At_Base(
            transitions, i, XAbstractTransition*);
        if (candidate == (const XAbstractTransition*)ignored)
            continue;
        if ((candidate->m_kind == XAbstractTransition_EventTransition
             || candidate->m_kind == XAbstractTransition_KeyEventTransition
             || candidate->m_kind == XAbstractTransition_MouseEventTransition)
            && ((XEventTransition*)candidate)->m_registered
            && ((XEventTransition*)candidate)->m_eventSource == source) {
            return true;
        }
    }

    const XVector* children = XState_childStates_const_internal(state);
    for (int64_t i = 0;
         children
             && i < (int64_t)XVector_size_base(
                 (const XContainer*)children); ++i) {
        XAbstractState* child = XVector_At_Base(children, i, XAbstractState*);
        if (child && (child->m_kind == XAbstractState_StandardState
                      || child->m_kind == XAbstractState_StateMachine)
            && XEventTransition_sourceIsRegistered(
                (XState*)child, ignored, source)) {
            return true;
        }
    }
    return false;
}

XVtable* XEventTransition_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XEventTransition)
	XCLASS_SET_CLASS_NAME_DEFAULT("XEventTransition");
    XVTABLE_INHERIT_XCLASS(XAbstractTransition);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractTransition_EventTest, VXEventTransition_eventTest);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractTransition_OnTransition, VXEventTransition_onTransition);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXEventTransition_deinit);
    return XVTABLE_DEFAULT;
}

XEventTransition* XEventTransition_create(void)
{
    return XEventTransition_create_ex(NULL, XEVENT_TYPE_NONE, NULL);
}

XEventTransition* XEventTransition_create_ex(XObject* object, XEventType type, XState* sourceState)
{
    XEventTransition* transition = XNew(XEventTransition);
    if (!transition)
        return NULL;
    XEventTransition_init_ex(transition, object, type, sourceState);
    Set_Class_MemoryFree(transition, XFree_System);
    return transition;
}

void XEventTransition_init(XEventTransition* transition)
{
    XEventTransition_init_ex(transition, NULL, XEVENT_TYPE_NONE, NULL);
}

void XEventTransition_init_ex(XEventTransition* transition, XObject* object,
                              XEventType type, XState* sourceState)
{
    if (!transition)
        return;

    XAbstractTransition_init((XAbstractTransition*)transition, sourceState);
    XClassSetVtable(transition, XEventTransition);
    transition->m_class.m_kind = XAbstractTransition_EventTransition;
    transition->m_eventSource = object;
    transition->m_eventType = type;
    transition->m_registered = false;

    if (sourceState
        && XStateMachine_isConfigured_internal(
            sourceState->m_class.m_machine, (XAbstractState*)sourceState)) {
        XEventTransition_register_internal(transition);
    }
}

XObject* XEventTransition_eventSource(const XEventTransition* transition)
{
    return transition ? transition->m_eventSource : NULL;
}

void XEventTransition_setEventSource(XEventTransition* transition, XObject* object)
{
    if (!transition || transition->m_eventSource == object)
        return;
    XEventTransition_unregister_internal(transition);
    transition->m_eventSource = object;
    XEventTransition_register_internal(transition);
}

XEventType XEventTransition_eventType(const XEventTransition* transition)
{
    return transition ? transition->m_eventType : XEVENT_TYPE_NONE;
}

void XEventTransition_setEventType(XEventTransition* transition, XEventType type)
{
    if (!transition || transition->m_eventType == type)
        return;
    transition->m_eventType = type;
}

void XEventTransition_register_internal(XEventTransition* transition)
{
    if (!transition || transition->m_registered || !transition->m_eventSource)
        return;
    XStateMachine* machine = XAbstractTransition_machine((XAbstractTransition*)transition);
    if (!machine || !transition->m_class.m_sourceState
        || !XStateMachine_isConfigured_internal(
            machine, (XAbstractState*)transition->m_class.m_sourceState)) {
        return;
    }

    XObject_installEventFilter(transition->m_eventSource, (XObject*)machine);
    transition->m_registered = true;
}

void XEventTransition_unregister_internal(XEventTransition* transition)
{
    if (!transition || !transition->m_registered || !transition->m_eventSource)
        return;

    XStateMachine* machine = XAbstractTransition_machine((XAbstractTransition*)transition);
    transition->m_registered = false;
    if (!machine)
        return;

    if (!XEventTransition_sourceIsRegistered(
            (XState*)machine, transition, transition->m_eventSource)) {
        XObject_removeEventFilter(transition->m_eventSource, (XObject*)machine);
    }
}
