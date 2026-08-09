#include "XKeyEventTransition.h"

#include "XMemory.h"
#include "XStateMachine_p.h"

#include <stdlib.h>

static bool VXKeyEventTransition_eventTest(XKeyEventTransition* transition, XEvent* event)
{
    bool matched = XVtableGetFunc(
        XEventTransition_class_init(), EXAbstractTransition_EventTest,
        bool(*)(XEventTransition*, XEvent*))((XEventTransition*)transition, event);
    if (!matched)
        return false;

    XEvent* wrappedEvent = XStateMachine_WrappedEvent_event(
        (XStateMachine_WrappedEvent*)event);
    if (!wrappedEvent || !XEvent_isInputEvent(wrappedEvent)
        || XEvent_isPointerEvent(wrappedEvent)) {
        return false;
    }

    XKeyEvent* keyEvent = (XKeyEvent*)wrappedEvent;
    return XKeyEvent_key(keyEvent) == transition->m_key
        && (XKeyEvent_modifiers(keyEvent) & transition->m_modifierMask)
            == transition->m_modifierMask;
}

static void VXKeyEventTransition_onTransition(XKeyEventTransition* transition,
                                              XEvent* event)
{
    XVtableGetFunc(
        XEventTransition_class_init(), EXAbstractTransition_OnTransition,
        void(*)(XEventTransition*, XEvent*))((XEventTransition*)transition, event);
}

XVtable* XKeyEventTransition_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XKeyEventTransition)
    XVTABLE_INHERIT_XCLASS(XEventTransition);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractTransition_EventTest, VXKeyEventTransition_eventTest);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractTransition_OnTransition, VXKeyEventTransition_onTransition);
    return XVTABLE_DEFAULT;
}

XKeyEventTransition* XKeyEventTransition_create(void)
{
    return XKeyEventTransition_create_ex(NULL, XEVENT_TYPE_NONE, 0, NULL);
}

XKeyEventTransition* XKeyEventTransition_create_ex(XObject* object, XEventType type,
                                                   int key, XState* sourceState)
{
    XKeyEventTransition* transition = XNew(XKeyEventTransition);
    if (!transition)
        return NULL;
    XKeyEventTransition_init_ex(transition, object, type, key, sourceState);
    Set_Class_MemoryFree(transition, XFree_System);
    return transition;
}

void XKeyEventTransition_init(XKeyEventTransition* transition)
{
    XKeyEventTransition_init_ex(transition, NULL, XEVENT_TYPE_NONE, 0, NULL);
}

void XKeyEventTransition_init_ex(XKeyEventTransition* transition, XObject* object,
                                 XEventType type, int key, XState* sourceState)
{
    if (!transition)
        return;
    XEventTransition_init_ex((XEventTransition*)transition, object, type, sourceState);
    XClassSetVtable(transition, XKeyEventTransition);
    transition->m_class.m_class.m_kind = XAbstractTransition_KeyEventTransition;
    transition->m_key = key;
    transition->m_modifierMask = XKeyboardModifier_NoModifier;
}

int XKeyEventTransition_key(const XKeyEventTransition* transition)
{
    return transition ? transition->m_key : 0;
}

void XKeyEventTransition_setKey(XKeyEventTransition* transition, int key)
{
    if (transition)
        transition->m_key = key;
}

XKeyboardModifiers XKeyEventTransition_modifierMask(const XKeyEventTransition* transition)
{
    return transition ? transition->m_modifierMask : XKeyboardModifier_NoModifier;
}

void XKeyEventTransition_setModifierMask(XKeyEventTransition* transition,
                                         XKeyboardModifiers modifiers)
{
    if (transition)
        transition->m_modifierMask = modifiers;
}
