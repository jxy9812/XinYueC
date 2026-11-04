#include "XState.h"

#include "XMemory.h"
#include "XSignalTransition.h"
#include "XStateMachine_p.h"
#include "XString.h"
#include "XStringUtils.h"

#include <string.h>

/** @brief 复制属性名的独立 UTF-8 文本（记录自持，避免与调用方共享 COW 缓冲）。 */
static char* XState_PropertyAssignment_copyName(const XString* name)
{
    if (!name)
        return NULL;
    /* XString_create_copy 与源共享底层数据、XString_toUtf8 返回内部缓存，两者都
     * 不能跨调用方释放存活；XStrdup 走库内分配器给出独立副本。 */
    return XStrdup(XString_toUtf8(name));
}

/** @brief 按 UTF-8 文本比较属性名。 */
static bool XState_PropertyAssignment_sameName(const char* left, const char* right)
{
    if (!left || !right)
        return false;
    return XStrcmp(left, right) == 0;
}

/** @brief 分配一条属性赋值记录（记录本身由容器持有指针）。 */
static XState_PropertyAssignment* XState_PropertyAssignment_create(
    XObject* object, const XString* name, const XVariant* value)
{
    XState_PropertyAssignment* assignment =
        XMemory_malloc(sizeof(XState_PropertyAssignment), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!assignment)
        return NULL;
    assignment->m_object = object;
    assignment->m_propertyName = XState_PropertyAssignment_copyName(name);
    assignment->m_value = value ? XVariant_create_copy(value) : NULL;
    if (!assignment->m_propertyName || (value && !assignment->m_value)) {
        if (assignment->m_propertyName)
            XMemory_free(assignment->m_propertyName, XCLASS_DEFAULT_MEMORY_TYPE);
        if (assignment->m_value)
            XClassDelete((XClass*)assignment->m_value);
        XMemory_free(assignment, XCLASS_DEFAULT_MEMORY_TYPE);
        return NULL;
    }
    return assignment;
}

/** @brief 释放一条属性赋值记录及其持有的属性名与赋值副本。 */
static void XState_PropertyAssignment_delete(XState_PropertyAssignment* assignment)
{
    if (!assignment)
        return;
    if (assignment->m_propertyName)
        XMemory_free(assignment->m_propertyName, XCLASS_DEFAULT_MEMORY_TYPE);
    if (assignment->m_value)
        XClassDelete((XClass*)assignment->m_value);
    XMemory_free(assignment, XCLASS_DEFAULT_MEMORY_TYPE);
}

static void VXState_deinit(XState* state)
{
    if (!state)
        return;

    if (state->m_transitions) {
        for (int64_t i = 0;
             i < (int64_t)XVector_size_base(
                 (const XContainer*)state->m_transitions); ++i) {
            XAbstractTransition* transition = XVector_At_Base(state->m_transitions, i, XAbstractTransition*);
            XStateMachine_unregisterTransition_internal(state->m_class.m_machine, transition, false); /* 析构路径:同步删 */
            transition->m_sourceState = NULL;
        }
        XClassDelete((XClass*)state->m_transitions);
        state->m_transitions = NULL;
    }

    if (state->m_propertyAssignments) {
        for (int64_t i = 0;
             i < (int64_t)XVector_size_base(
                 (const XContainer*)state->m_propertyAssignments); ++i) {
            XState_PropertyAssignment* assignment = XVector_At_Base(
                state->m_propertyAssignments, i, XState_PropertyAssignment*);
            XState_PropertyAssignment_delete(assignment);
        }
        XClassDelete((XClass*)state->m_propertyAssignments);
        state->m_propertyAssignments = NULL;
    }

    if (state->m_childStates) {
        for (int64_t i = 0;
             i < (int64_t)XVector_size_base(
                 (const XContainer*)state->m_childStates); ++i) {
            XAbstractState* child = XVector_At_Base(state->m_childStates, i, XAbstractState*);
            child->m_parentState = NULL;
            XAbstractState_setMachine_internal(child, NULL);
        }
        XClassDelete((XClass*)state->m_childStates);
        state->m_childStates = NULL;
    }

    state->m_initialState = NULL;
    state->m_errorState = NULL;
    XVtableGetFunc(XAbstractState_class_init(), EXClass_Deinit,
                   void(*)(XAbstractState*))((XAbstractState*)state);
}

XVtable* XState_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XState)
    XVTABLE_INHERIT_XCLASS(XAbstractState);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXState_deinit);
    return XVTABLE_DEFAULT;
}

XState* XState_create_ex(XMemoryType memory, XState_ChildMode childMode, XState* parent)
{
    XState* state = XMemory_malloc(sizeof(XState), memory);
    if (!state)
        return NULL;
    XState_init_ex(state, childMode, parent);
    Set_Class_Memory(state, memory); Set_Class_IsHeap(state, true);
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
    XAssert(state->m_childStates, "Memory allocation failed");
    state->m_transitions = XVector_Create(XAbstractTransition*);
    XAssert(state->m_transitions, "Memory allocation failed");
    state->m_propertyAssignments = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(XState_PropertyAssignment*), false);
    XAssert(state->m_propertyAssignments, "Memory allocation failed");
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
    /* 只有 XState 与状态机根节点持有子状态表。 */
    if (!state
        || (state->m_class.m_kind != XAbstractState_StandardState
            && state->m_class.m_kind != XAbstractState_StateMachine)) {
        return NULL;
    }
    return state->m_childStates;
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

/** @brief 对标 QState::addTransition 的目标校验：双方都有状态机且不同 → 拒绝。 */
static bool XState_targetMachineOk(const XState* state, const XAbstractState* target)
{
    return !(target && target->m_machine && state->m_class.m_machine
             && state->m_class.m_machine != target->m_machine);
}

XSignalTransition* XState_addTransition_2(XState* state, const XObject* sender,
                                          size_t signal, XAbstractState* target)
{
    if (!state || !sender || !signal || !target)
        return NULL;
    /* 对标 Qt addTransition(sender, signal, target)：目标跨状态机时拒绝
     * 并返回 NULL（Qt 侧由 addTransition(trans) 校验，此处提前同口径）。 */
    if (!XState_targetMachineOk(state, target))
        return NULL;

    XSignalTransition* transition = XSignalTransition_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sender, signal, state);
    if (!transition)
        return NULL;
    if (!XAbstractTransition_setTargetState((XAbstractTransition*)transition, target)) {
        XClassDelete((XClass*)transition);
        return NULL;
    }
    return transition;
}

XAbstractTransition* XState_addTransition_3(XState* state, XAbstractState* target)
{
    if (!state || !target)
        return NULL;
    /* 对标 Qt addTransition(QAbstractState* target)：目标跨状态机时拒绝。 */
    if (!XState_targetMachineOk(state, target))
        return NULL;

    XAbstractTransition* transition = XClass_Malloc(XAbstractTransition);
    if (!transition)
        return NULL;
    XAbstractTransition_init(transition, state);
    Set_Class_IsHeap(transition, true);
    if (!XAbstractTransition_setTargetState(transition, target)) {
        XClassDelete((XClass*)transition);
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
        XStateMachine_unregisterTransition_internal(state->m_class.m_machine, transition, true); /* 运行期:延迟回收 */
    XVector_removeAt_base(state->m_transitions, index);
    transition->m_sourceState = NULL;
    if (XObject_parent((XObject*)transition) == (XObject*)state)
        XObject_setParent((XObject*)transition, NULL);
    return true;
}

const XVector* XState_transitions_const(const XState* state)
{
    /* 只有 XState 与状态机根节点持有出站转换表。 */
    if (!state
        || (state->m_class.m_kind != XAbstractState_StandardState
            && state->m_class.m_kind != XAbstractState_StateMachine)) {
        return NULL;
    }
    return state->m_transitions;
}

const XVector* XState_childStates_const(const XState* state)
{
    return state ? XState_childStates_const_internal(state) : NULL;
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

bool XState_assignProperty(XState* state, XObject* object, const XString* name,
                           const XVariant* value)
{
    if (!state || !object || !name || !state->m_propertyAssignments)
        return false;

    /* 同名同对象原地更新赋值（对标 QPropertyAssignment::hasTarget 分支） */
    for (int64_t i = 0;
         i < (int64_t)XVector_size_base(
             (const XContainer*)state->m_propertyAssignments); ++i) {
        XState_PropertyAssignment* assignment = XVector_At_Base(
            state->m_propertyAssignments, i, XState_PropertyAssignment*);
        if (!assignment || assignment->m_object != object
            || !XState_PropertyAssignment_sameName(assignment->m_propertyName,
                                                   XString_toUtf8(name))) {
            continue;
        }
        XVariant* copy = value ? XVariant_create_copy(value) : NULL;
        if (value && !copy)
            return false;
        if (assignment->m_value)
            XClassDelete((XClass*)assignment->m_value);
        assignment->m_value = copy;
        return true;
    }

    XState_PropertyAssignment* assignment =
        XState_PropertyAssignment_create(object, name, value);
    if (!assignment)
        return false;
    if (!XVector_push_back_1_base(state->m_propertyAssignments, &assignment)) {
        XState_PropertyAssignment_delete(assignment);
        return false;
    }
    return true;
}

const XVector* XState_propertyAssignments_const(const XState* state)
{
    /* 最终/历史状态的存储布局没有该成员，非标准状态一律视为无赋值表。 */
    if (!state
        || (state->m_class.m_kind != XAbstractState_StandardState
            && state->m_class.m_kind != XAbstractState_StateMachine)) {
        return NULL;
    }
    return state->m_propertyAssignments;
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
