#include "XAbstractTransition.h"

#include "XMemory.h"
#include "XStateMachine_p.h"


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
        XStateMachine_unregisterTransition_internal(machine, transition, false); /* 析构路径:同步删 */
    if (transition->m_sourceState)
        XState_removeTransition(transition->m_sourceState, transition);
    if (transition->m_targetStates) {
        XClassDelete((XClass*)transition->m_targetStates);
        transition->m_targetStates = NULL;
    }
    XVtableGetFunc(XObject_class_init(), EXClass_Deinit,
                   void(*)(XObject*))((XObject*)transition);
}

XVtable* XAbstractTransition_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractTransition)
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
    XAssert(transition->m_targetStates, "Memory allocation failed");
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
    const int64_t size = (int64_t)XVector_size_base(
        (const XContainer*)transition->m_targetStates);
    /* 对标 Qt：单目标且与现状相同（含"都为空"）时直接返回，不发信号。 */
    if (previous == target && (target ? size == 1 : size == 0))
        return true;

    XVector_clear_base((XContainer*)transition->m_targetStates);
    if (target && !XVector_push_back_1_base(transition->m_targetStates, &target))
        return false;

    /* Qt 此时只发 targetStateChanged；targetStatesChanged 由 setTargetStates 转出。 */
    XAbstractTransition_targetStateChanged_signal(transition);
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

    /* 对标 Qt 的多重集比较：顺序不同但内容一致视为未变化，不发信号。 */
    const int64_t targetCount = targets
        ? (int64_t)XVector_size_base((const XContainer*)targets)
        : 0;
    const int64_t currentCount = (int64_t)XVector_size_base(
        (const XContainer*)transition->m_targetStates);
    bool sameList = true;
    if (targetCount == currentCount) {
        for (int64_t i = 0; i < targetCount && sameList; ++i) {
            XAbstractState* candidate = XVector_At_Base(targets, i, XAbstractState*);
            bool matched = false;
            for (int64_t j = 0; j < currentCount; ++j) {
                if (XVector_At_Base(transition->m_targetStates, j, XAbstractState*) != candidate)
                    continue;
                /* 同一元素只能配对一次：已配对过就跳过，避免 [a,a] 与 [a,b] 判等。 */
                bool alreadyPaired = false;
                for (int64_t k = 0; k < i && !alreadyPaired; ++k) {
                    if (XVector_At_Base(targets, k, XAbstractState*) == candidate)
                        alreadyPaired = true;
                }
                if (alreadyPaired)
                    continue;
                matched = true;
                break;
            }
            if (!matched)
                sameList = false;
        }
    } else {
        sameList = false;
    }
    if (sameList)
        return true;

    XAbstractState* previous = XAbstractTransition_targetState(transition);
    XVector_clear_base((XContainer*)transition->m_targetStates);
    if (targets && !XVector_push_back_3(transition->m_targetStates, targets))
        return false;

    /* Qt 的 setTargetStates 只发 targetStatesChanged；targetStateChanged 属于
     * setTargetState 的路径。C 侧保持同一分工，避免一次写入两条通知。 */
    (void)previous;
    XAbstractTransition_targetStatesChanged_signal(transition);
    return true;
}

XAbstractTransition_TransitionType XAbstractTransition_transitionType(const XAbstractTransition* transition)
{
    return transition ? transition->m_transitionType
                      : XAbstractTransition_ExternalTransition;
}

void XAbstractTransition_setTransitionType(XAbstractTransition* transition, XAbstractTransition_TransitionType type)
{
    if (transition)
        transition->m_transitionType = type;
}

XStateMachine* XAbstractTransition_machine(const XAbstractTransition* transition)
{
    if (!transition)
        return NULL;
    if (transition->m_sourceState)
        return XAbstractState_machine((XAbstractState*)transition->m_sourceState);
    /* 对标 QAbstractTransitionPrivate::machine：源状态缺失时，历史状态的默认
     * 转换挂在该历史状态下，归属要沿父对象回推。其余情况（无源状态的普通
     * 转换）与 Qt 一致返回 NULL。 */
    if (transition->m_kind == XAbstractTransition_CustomTransition) {
        XObject* parent = XObject_parent((XObject*)transition);
        XAbstractState* state = (XAbstractState*)parent;
        if (state && state->m_kind == XAbstractState_HistoryState)
            return XAbstractState_machine(state);
    }
    return NULL;
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
