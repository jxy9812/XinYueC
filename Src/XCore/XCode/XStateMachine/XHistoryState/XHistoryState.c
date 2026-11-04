#include "XHistoryState.h"

#include "XAbstractTransition.h"
#include "XMemory.h"
#include "XStateMachine_p.h"


static void VXHistoryState_deinit(XHistoryState* state)
{
    if (!state)
        return;

    /* 默认转换挂在历史状态下（对标 Qt 的 setParent(this)），析构时要一并回收；
     * 它没有源状态，走不到 XState 的转换回收路径。 */
    if (state->m_defaultTransition) {
        XAbstractTransition* transition = state->m_defaultTransition;
        state->m_defaultTransition = NULL;
        XClassDelete((XClass*)transition);
    }
    if (state->m_configuration) {
        XClassDelete((XClass*)state->m_configuration);
        state->m_configuration = NULL;
    }
    XVtableGetFunc(XAbstractState_class_init(), EXClass_Deinit,
                   void(*)(XAbstractState*))((XAbstractState*)state);
}

XVtable* XHistoryState_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHistoryState)
    XVTABLE_INHERIT_XCLASS(XAbstractState);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHistoryState_deinit);
    return XVTABLE_DEFAULT;
}

XHistoryState* XHistoryState_create_ex(XMemoryType memory, XHistoryState_HistoryType type, XState* parent)
{
    XHistoryState* state = XMemory_malloc(sizeof(XHistoryState), memory);
    if (!state)
        return NULL;
    XHistoryState_init_ex(state, type, parent);
    Set_Class_Memory(state, memory); Set_Class_IsHeap(state, true);
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
    XAssert(state->m_configuration, "Memory allocation failed");
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

    XAbstractTransition* previous = state->m_defaultTransition;
    state->m_defaultTransition = transition;
    if (transition) {
        if (transition->m_sourceState)
            XState_removeTransition(transition->m_sourceState, transition);
        XObject_setParent((XObject*)transition, (XObject*)state);
    }
    /* Qt 用 setParent 表达所有权：被替换的旧转换随历史状态析构级联回收。
     * 这里被替换的旧转换已不挂在任何状态上，为防悬垂立即回收（调用方此后
     * 不得再引用旧转换指针）。 */
    if (previous)
        XClassDelete((XClass*)previous);
    /* 对标 Qt：setDefaultTransition 只发 defaultTransitionChanged
     *（defaultStateChanged 属于 setDefaultState 路径）。 */
    XHistoryState_defaultTransitionChanged_signal(state);
    return true;
}

/** @brief setDefaultState 的新建路径：接管语义与 setDefaultTransition 相同，
 *  但按 Qt 语义只发 defaultStateChanged（Qt 直设 binding 不发 transitionChanged）。 */
static bool XHistoryState_takeDefaultTransition(XHistoryState* state,
                                                XAbstractTransition* transition)
{
    XAbstractTransition* previous = state->m_defaultTransition;
    state->m_defaultTransition = transition;
    if (transition) {
        if (transition->m_sourceState)
            XState_removeTransition(transition->m_sourceState, transition);
        XObject_setParent((XObject*)transition, (XObject*)state);
    }
    if (previous)
        XClassDelete((XClass*)previous);
    return true;
}

XAbstractState* XHistoryState_defaultState(const XHistoryState* state)
{
    return state && state->m_defaultTransition
        ? XAbstractTransition_targetState(state->m_defaultTransition)
        : NULL;
}

/** @brief setDefaultState 生成的单目标默认转换（对标 Qt 的 DefaultStateTransition）。 */
typedef struct XHistoryState_DefaultTransition {
    XAbstractTransition m_class; ///< 继承 XAbstractTransition。
} XHistoryState_DefaultTransition;

XCLASS_DEFINE_BEGING(XHistoryState_DefaultTransition)
XCLASS_DEFINE_EXTEND_END(XHistoryState_DefaultTransition, XAbstractTransition)

static bool VXHistoryState_DefaultTransition_eventTest(XAbstractTransition* transition,
                                                       XEvent* event)
{
    (void)transition;
    (void)event;
    /* 永不参与事件匹配：历史入口由状态机当作特例直接展开。 */
    return false;
}

static void VXHistoryState_DefaultTransition_onTransition(XAbstractTransition* transition,
                                                          XEvent* event)
{
    (void)transition;
    (void)event;
}

XVtable* XHistoryState_DefaultTransition_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHistoryState_DefaultTransition)
    XVTABLE_INHERIT_XCLASS(XAbstractTransition);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractTransition_EventTest,
                             VXHistoryState_DefaultTransition_eventTest);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractTransition_OnTransition,
                             VXHistoryState_DefaultTransition_onTransition);
    return XVTABLE_DEFAULT;
}

/** @brief 判断转换是否为本模块 setDefaultState 生成的单目标默认转换。 */
static bool XHistoryState_isDefaultTransition(const XAbstractTransition* transition)
{
    return transition
        && XClassGetVtable(transition) == XHistoryState_DefaultTransition_class_init();
}

bool XHistoryState_setDefaultState(XHistoryState* state, XAbstractState* defaultState)
{
    if (!state)
        return false;
    /* 对标 Qt：state 必须是历史状态的兄弟（同属一个父状态）。 */
    if (defaultState && defaultState->m_parentState != state->m_class.m_parentState)
        return false;
    if (XHistoryState_defaultState(state) == defaultState
        && state->m_defaultTransition
        && XVector_size_base(
            (const XContainer*)state->m_defaultTransition->m_targetStates) == 1) {
        return true;
    }

    XAbstractTransition* current = state->m_defaultTransition;
    if (current && XHistoryState_isDefaultTransition(current)) {
        /* 复用自己生成的转换，只换目标（对标 Qt 的 defaultTransition->setTargetState）。 */
        if (!XAbstractTransition_setTargetState(current, defaultState))
            return false;
        XHistoryState_defaultStateChanged_signal(state);
        return true;
    }

    XAbstractTransition* transition = XClass_Malloc(XAbstractTransition);
    if (!transition)
        return false;
    XAbstractTransition_init(transition, NULL);
    Set_Class_IsHeap(transition, true);
    XClassGetVtable(transition) = XHistoryState_DefaultTransition_class_init();
    if (!XAbstractTransition_setTargetState(transition, defaultState)) {
        XClassDelete((XClass*)transition);
        return false;
    }
    /* 对标 Qt：setDefaultState 直设默认转换，只发 defaultStateChanged
     *（不发 defaultTransitionChanged）。 */
    XHistoryState_takeDefaultTransition(state, transition);
    XHistoryState_defaultStateChanged_signal(state);
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
    /* 对标 Qt 6.8：setHistoryType 只改类型，不清空已保存的历史配置
     * （qhistorystate.cpp 的实现仅赋值；清空只发生在状态机启动与父状态
     * 退出重录时）。 */
    state->m_historyType = type;
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
