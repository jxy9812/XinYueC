#include "XSignalTransition.h"

#include "XMemory.h"
#include "XStateMachine_p.h"

#include <stdlib.h>

static bool VXSignalTransition_eventTest(XSignalTransition* transition, XEvent* event)
{
    if (!transition || !event || XEvent_type(event) != XStateMachine_signalEventType_internal())
        return false;

    XStateMachine_SignalEvent* signalEvent = (XStateMachine_SignalEvent*)event;
    return signalEvent->m_sender == transition->m_senderObject
        && signalEvent->m_signal == transition->m_signal;
}

static void VXSignalTransition_onTransition(XSignalTransition* transition, XEvent* event)
{
    (void)transition;
    (void)event;
}

static void VXSignalTransition_deinit(XSignalTransition* transition)
{
    if (!transition)
        return;
    XSignalTransition_unregister_internal(transition);
    transition->m_senderObject = NULL;
    transition->m_signal = 0;
    XVtableGetFunc(XAbstractTransition_class_init(), EXClass_Deinit,
                   void(*)(XAbstractTransition*))(
                       (XAbstractTransition*)transition);
}

XVtable* XSignalTransition_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSignalTransition)
    XVTABLE_INHERIT_XCLASS(XAbstractTransition);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractTransition_EventTest, VXSignalTransition_eventTest);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractTransition_OnTransition, VXSignalTransition_onTransition);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSignalTransition_deinit);
    return XVTABLE_DEFAULT;
}

XSignalTransition* XSignalTransition_create(void)
{
    return XSignalTransition_create_ex(NULL, 0, NULL);
}

XSignalTransition* XSignalTransition_create_ex(const XObject* sender, size_t signal, XState* sourceState)
{
    XSignalTransition* transition = XNew(XSignalTransition);
    if (!transition)
        return NULL;
    XSignalTransition_init_ex(transition, sender, signal, sourceState);
    Set_Class_MemoryFree(transition, XFree_System);
    return transition;
}

void XSignalTransition_init(XSignalTransition* transition)
{
    XSignalTransition_init_ex(transition, NULL, 0, NULL);
}

void XSignalTransition_init_ex(XSignalTransition* transition, const XObject* sender,
                               size_t signal, XState* sourceState)
{
    if (!transition)
        return;

    XAbstractTransition_init((XAbstractTransition*)transition, sourceState);
    XClassSetVtable(transition, XSignalTransition);
    transition->m_class.m_kind = XAbstractTransition_SignalTransition;
    transition->m_senderObject = sender;
    transition->m_signal = signal;
    transition->m_connection = NULL;
    transition->m_registeredMachine = NULL;

    if (sourceState
        && XStateMachine_isConfigured_internal(
            sourceState->m_class.m_machine, (XAbstractState*)sourceState)) {
        XSignalTransition_register_internal(transition);
    }
}

const XObject* XSignalTransition_senderObject(const XSignalTransition* transition)
{
    return transition ? transition->m_senderObject : NULL;
}

void XSignalTransition_setSenderObject(XSignalTransition* transition, const XObject* sender)
{
    if (!transition || transition->m_senderObject == sender)
        return;

    XSignalTransition_unregister_internal(transition);
    transition->m_senderObject = sender;
    XSignalTransition_register_internal(transition);
    XSignalTransition_senderObjectChanged_signal(transition);
}

size_t XSignalTransition_signal(const XSignalTransition* transition)
{
    return transition ? transition->m_signal : 0;
}

void XSignalTransition_setSignal(XSignalTransition* transition, size_t signal)
{
    if (!transition || transition->m_signal == signal)
        return;

    XSignalTransition_unregister_internal(transition);
    transition->m_signal = signal;
    XSignalTransition_register_internal(transition);
    XSignalTransition_signalChanged_signal(transition);
}

void XSignalTransition_register_internal(XSignalTransition* transition)
{
    if (!transition || transition->m_connection || !transition->m_senderObject || !transition->m_signal)
        return;
    XStateMachine* machine = XAbstractTransition_machine((XAbstractTransition*)transition);
    if (!machine || !transition->m_class.m_sourceState
        || !XStateMachine_isConfigured_internal(
            machine, (XAbstractState*)transition->m_class.m_sourceState)) {
        return;
    }

    XStateMachine_registerSignalTransition_internal(machine, transition);
}

void XSignalTransition_unregister_internal(XSignalTransition* transition)
{
    if (!transition || !transition->m_connection)
        return;
    XStateMachine_unregisterSignalTransition_internal(
        transition->m_registeredMachine, transition);
}

void* XSignalTransition_senderObjectChanged_signal(XSignalTransition* transition)
{
    XEmitSignal((XObject*)transition,
                XSignalTransition_senderObjectChanged_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSignalTransition_signalChanged_signal(XSignalTransition* transition)
{
    XEmitSignal((XObject*)transition, XSignalTransition_signalChanged_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
