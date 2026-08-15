#include "XMouseEventTransition.h"

#include "XMemory.h"
#include "XStateMachine_p.h"

#include <stdlib.h>

static bool XMouseEventTransition_pathContains(const XVector* path, XPoint point)
{
    int64_t count = path
        ? (int64_t)XVector_size_base((const XContainer*)path)
        : 0;
    if (count == 0)
        return true;
    if (count < 3)
        return false;

    bool inside = false;
    for (int64_t i = 0, j = count - 1; i < count; j = i++) {
        XPoint first = XVector_At_Base(path, i, XPoint);
        XPoint second = XVector_At_Base(path, j, XPoint);
        bool crosses = (first.y > point.y) != (second.y > point.y);
        if (crosses) {
            double edgeX = (double)(second.x - first.x) * (point.y - first.y)
                / (double)(second.y - first.y) + first.x;
            if (point.x < edgeX)
                inside = !inside;
        }
    }
    return inside;
}

static bool VXMouseEventTransition_eventTest(XMouseEventTransition* transition,
                                             XEvent* event)
{
    bool matched = XVtableGetFunc(
        XEventTransition_class_init(), EXAbstractTransition_EventTest,
        bool(*)(XEventTransition*, XEvent*))((XEventTransition*)transition, event);
    if (!matched)
        return false;

    XEvent* wrappedEvent = XStateMachine_WrappedEvent_event(
        (XStateMachine_WrappedEvent*)event);
    if (!wrappedEvent || !XEvent_isPointerEvent(wrappedEvent)
        || !XEvent_isSinglePointEvent(wrappedEvent)) {
        return false;
    }

    XMouseEvent* mouseEvent = (XMouseEvent*)wrappedEvent;
    return XMouseEvent_button(mouseEvent) == transition->m_button
        && (XMouseEvent_modifiers(mouseEvent) & transition->m_modifierMask)
            == transition->m_modifierMask
        && XMouseEventTransition_pathContains(
            transition->m_hitTestPath, XMouseEvent_position(mouseEvent));
}

static void VXMouseEventTransition_onTransition(XMouseEventTransition* transition,
                                                XEvent* event)
{
    XVtableGetFunc(
        XEventTransition_class_init(), EXAbstractTransition_OnTransition,
        void(*)(XEventTransition*, XEvent*))((XEventTransition*)transition, event);
}

static void VXMouseEventTransition_deinit(XMouseEventTransition* transition)
{
    if (!transition)
        return;
    if (transition->m_hitTestPath) {
        XVector_delete_base((XClass*)transition->m_hitTestPath);
        transition->m_hitTestPath = NULL;
    }
    XVtableGetFunc(XEventTransition_class_init(), EXClass_Deinit,
                   void(*)(XEventTransition*))((XEventTransition*)transition);
}

XVtable* XMouseEventTransition_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMouseEventTransition)
    XVTABLE_INHERIT_XCLASS(XEventTransition);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractTransition_EventTest, VXMouseEventTransition_eventTest);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractTransition_OnTransition, VXMouseEventTransition_onTransition);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMouseEventTransition_deinit);
    return XVTABLE_DEFAULT;
}

XMouseEventTransition* XMouseEventTransition_create_ex(XMemoryType memory, XObject* object, XEventType type,
                                                       XMouseButton button,
                                                       XState* sourceState)
{
    XMouseEventTransition* transition = XMemory_malloc(sizeof(XMouseEventTransition), memory);
    if (!transition)
        return NULL;
    XMouseEventTransition_init_ex(transition, object, type, button, sourceState);
    Set_Class_Memory(transition, memory); Set_Class_IsHeap(transition, true);
    return transition;
}

void XMouseEventTransition_init(XMouseEventTransition* transition)
{
    XMouseEventTransition_init_ex(
        transition, NULL, XEVENT_TYPE_NONE, XMouseButton_NoButton, NULL);
}

void XMouseEventTransition_init_ex(XMouseEventTransition* transition, XObject* object,
                                   XEventType type, XMouseButton button,
                                   XState* sourceState)
{
    if (!transition)
        return;
    XEventTransition_init_ex((XEventTransition*)transition, object, type, sourceState);
    XClassSetVtable(transition, XMouseEventTransition);
    transition->m_class.m_class.m_kind = XAbstractTransition_MouseEventTransition;
    transition->m_button = button;
    transition->m_modifierMask = XKeyboardModifier_NoModifier;
    transition->m_hitTestPath = XVector_Create(XPoint);
}

XMouseButton XMouseEventTransition_button(const XMouseEventTransition* transition)
{
    return transition ? transition->m_button : XMouseButton_NoButton;
}

void XMouseEventTransition_setButton(XMouseEventTransition* transition, XMouseButton button)
{
    if (transition)
        transition->m_button = button;
}

XKeyboardModifiers XMouseEventTransition_modifierMask(const XMouseEventTransition* transition)
{
    return transition ? transition->m_modifierMask : XKeyboardModifier_NoModifier;
}

void XMouseEventTransition_setModifierMask(XMouseEventTransition* transition,
                                           XKeyboardModifiers modifiers)
{
    if (transition)
        transition->m_modifierMask = modifiers;
}

const XVector* XMouseEventTransition_hitTestPath_const(
    const XMouseEventTransition* transition)
{
    return transition ? transition->m_hitTestPath : NULL;
}

bool XMouseEventTransition_setHitTestPath(XMouseEventTransition* transition,
                                          const XVector* path)
{
    if (!transition || (path && XContainerTypeSize(path) != sizeof(XPoint)))
        return false;
    XVector* copy = path ? XVector_create_copy(path) : XVector_Create(XPoint);
    if (!copy)
        return false;
    XVector_delete_base((XClass*)transition->m_hitTestPath);
    transition->m_hitTestPath = copy;
    return true;
}
