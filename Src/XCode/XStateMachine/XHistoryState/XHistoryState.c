#include "XHistoryState.h"
#include "XState.h"
#include "XStateMachine.h"
#include "XMemory.h"
#include "XVector.h"
void XStateMachine_enterState(XStateMachine* machine, XAbstractState* state);
void XStateMachine_exitState(XStateMachine* machine, XAbstractState* state);

// 辅助函数：递归收集深层历史状态（从父状态开始的所有活跃子状态）
static void collectDeepHistory(XAbstractState* state, XVector* result);
// 辅助函数：获取父状态的直接活跃子状态（用于浅层历史）
static XAbstractState* getParentActiveChild(XState* parent);

static void VXState_activate(XHistoryState* state);
static void VXState_deactivate(XHistoryState* state);
static void VHistoryState_setParentState(XHistoryState* state, XState* parent);
static void VXHistoryState_deinit(XHistoryState* state);

/**
 * @brief 初始化历史状态的虚函数表
 */
XVtable* XHistoryState_class_init()
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XHistoryState))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_DEFAULT(XAbstractState_class_init());

    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHistoryState_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_SetParentState, VHistoryState_setParentState);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_Activate, VXState_activate);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_Deactivate, VXState_deactivate);
#if SHOWCONTAINERSIZE
    printf("XHistoryState size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

/**
 * @brief 创建历史状态实例
 */
XHistoryState* XHistoryState_create(XHistoryStateType type) {
    XHistoryState* state = (XHistoryState*)XMemory_malloc(sizeof(XHistoryState));
    if (state) {
        XHistoryState_init(state, type);
        Set_Class_MemoryFree(state, XFree);
    }
    return state;
}

/**
 * @brief 初始化历史状态
 */
void XHistoryState_init(XHistoryState* state, XHistoryStateType type) {
    if (!state) return;
    XAbstractState_init(&state->m_class, XStateType_History);
    state->m_historyType = type;
    state->m_defaultState = NULL;
    state->m_storedShallow = NULL;
    state->m_storedDeep = XVector_create(sizeof(XAbstractState*));
    //state->m_activatedTarget = NULL;
}

/**
 * @brief 获取历史状态类型
 */
XHistoryStateType XHistoryState_historyType(const XHistoryState* state) {
    return state ? state->m_historyType : XHistoryStateType_Shallow;
}

/**
 * @brief 设置默认状态（无历史时恢复此状态）
 */
void XHistoryState_setDefaultState(XHistoryState* state, XAbstractState* defaultState) {
    if (!state || !defaultState) return;

    XState* parent = XAbstractState_parentState((XAbstractState*)state);
    if (!parent || !XState_isChild(parent, defaultState)) {
        return;  // 非法默认状态，不设置
    }
    state->m_defaultState = defaultState;
}

/**
 * @brief 获取默认状态
 */
XAbstractState* XHistoryState_defaultState(const XHistoryState* state) {
    return state ? state->m_defaultState : NULL;
}

/**
 * @brief 手动存储指定状态作为历史记录
 */
void XHistoryState_storeState(XHistoryState* state, XAbstractState* storedState) {
    if (!state || !storedState) return;
    XState* parent = XAbstractState_parentState((XAbstractState*)state);
    if (!parent) return;

    if (!XState_isChild(parent, storedState)) return;

    switch (state->m_historyType) {
    case XHistoryStateType_Shallow:
        if (XAbstractState_parentState(storedState) == (XAbstractState*)parent) {
            state->m_storedShallow = storedState;
        }
        break;
    case XHistoryStateType_Deep:
        XVector_clear_base(state->m_storedDeep);
        collectDeepHistory((XAbstractState*)parent, state->m_storedDeep);
        break;
    default:
        break;
    }
}

/**
 * @brief 自动存储当前父状态下的活跃子状态作为历史记录
 * 修复点：确保只收集处于运行状态的子状态
 */
void XHistoryState_storeCurrentState(XHistoryState* state) {
    if (!state) return;
    XState* parent = XAbstractState_parentState((XAbstractState*)state);
    if (!parent) return;

    if (state->m_historyType == XHistoryStateType_Shallow) {
        state->m_storedShallow = getParentActiveChild(parent);
    }
    else {
        XVector_clear_base(state->m_storedDeep);
        // 只在父状态运行时才收集深层历史，避免存储已退出的状态
        if (parent->m_class.m_isRunning) {
            collectDeepHistory((XAbstractState*)parent, state->m_storedDeep);
        }
    }
}

/**
 * @brief 激活历史状态（计算实际目标状态并存储）
 */
void VXState_activate(XHistoryState* state)
{
    if (!state) return;

    XAbstractState* actualTarget = state;
    XVector* deepHistoryChain = NULL;
    bool isDeepHistory = false;
    XHistoryState* history = (XHistoryState*)state;
    actualTarget = XHistoryState_getActivatedTarget(history);

    if (history->m_historyType == XHistoryStateType_Deep) {
        deepHistoryChain = XHistoryState_storedDeep(history);
        isDeepHistory = true;
    }

    if (!actualTarget) return ;

    // 激活深层历史链
    if (deepHistoryChain && XVector_size_base(deepHistoryChain) > 0) {
        // 按顺序激活链中所有状态（从顶层到深层）
        for (size_t i = 0; i < XVector_size_base(deepHistoryChain); i++) {
            XAbstractState** statePtr = XVector_at_base(deepHistoryChain, i);
            XAbstractState* stateInChain = *statePtr;

            if (!stateInChain || stateInChain->m_isRunning) continue;

            // 对于基本状态，跳过初始状态激活，避免覆盖历史
            if (stateInChain->m_type == XStateType_Basic) {
                ((XState*)stateInChain)->m_skipInitialState = true;
            }

            XStateMachine_enterState(((XAbstractState*)state)->m_machine, stateInChain);
        }
    }
    else {
        // 非深层历史：激活实际目标
        XStateMachine_enterState(((XAbstractState*)state)->m_machine, actualTarget);
    }
}

void VXState_deactivate(XHistoryState* state)
{
}

/**
 * @brief 获取深层历史存储的状态链
 */
XVector* XHistoryState_storedDeep(const XHistoryState* state) {
    return state ? state->m_storedDeep : NULL;
}

/**
 * @brief 计算实际要激活的目标状态
 */
XAbstractState* XHistoryState_getActivatedTarget(XHistoryState* state) {
    XState* parent = XAbstractState_parentState((XAbstractState*)state);
    if (!parent) return state->m_defaultState;

    if (state->m_historyType == XHistoryStateType_Shallow) {
        return state->m_storedShallow ? state->m_storedShallow : state->m_defaultState;
    }
    else {
        // 深层历史：如果有存储的状态链，返回链的第一个有效状态
        if (XVector_size_base(state->m_storedDeep) > 0) {
            return *(XAbstractState**)XVector_at_base(state->m_storedDeep, 0);
        }
        return state->m_defaultState;
    }
}

/**
 * @brief 递归收集深层历史的活跃状态链
 * 修复点：确保按正确层级顺序收集，只包含活跃状态
 */
static void collectDeepHistory(XAbstractState* state, XVector* result) {
    if (!state || !result || !state->m_isRunning) return;

    // 先添加当前状态，再递归添加子状态，保证父->子顺序
    XVector_push_back_base(result, &state);

    // 只处理基本状态的子状态
    if (state->m_type == XStateType_Basic) {
        XState* basicState = (XState*)state;
        for (size_t i = 0; i < XState_childCount(basicState); i++) {
            XAbstractState* child = XState_child(basicState, i);
            // 只收集活跃的子状态
            if (child->m_isRunning) {
                collectDeepHistory(child, result);
            }
        }
    }
}

/**
 * @brief 获取父状态的直接活跃子状态
 */
static XAbstractState* getParentActiveChild(XState* parent) {
    if (!parent) return NULL;
    for (size_t i = 0; i < XState_childCount(parent); i++) {
        XAbstractState* child = XState_child(parent, i);
        if (child->m_isRunning) return child;
    }
    return NULL;
}

/**
 * @brief 设置历史状态的父状态
 */
static void VHistoryState_setParentState(XHistoryState* state, XState* parent) {
    if (!state || !parent) return;
    ((XAbstractState*)state)->m_parentState = (XAbstractState*)parent;
    XAbstractState_setParentState_base(&state->m_class, (XAbstractState*)parent);
    XAbstractState_setMachine_base(state, XAbstractState_machine((XAbstractState*)parent));

    if (state->m_defaultState && !XState_isChild(parent, state->m_defaultState)) {
        state->m_defaultState = NULL;
    }
}

/**
 * @brief 销毁历史状态（释放资源）
 */
static void VXHistoryState_deinit(XHistoryState* state) {
    if (state->m_storedDeep) {
        XVector_delete_base(state->m_storedDeep);
        state->m_storedDeep = NULL;
    }
    XVtableGetFunc(XAbstractState_class_init(), EXClass_Deinit, void(*)(XAbstractState*))(state);
}
