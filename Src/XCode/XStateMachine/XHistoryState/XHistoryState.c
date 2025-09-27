#include "XHistoryState.h"
#include "XState.h"
#include "XStateMachine.h"
#include "XMemory.h"
#include "XVector.h"

// 辅助函数：递归收集深层历史状态（从父状态开始的所有活跃子状态）
static void collectDeepHistory(XAbstractState* state, XVector* result);
// 辅助函数：获取父状态的直接活跃子状态（用于浅层历史）
static XAbstractState* getParentActiveChild(XState* parent);
// 新增：获取历史状态实际应激活的目标状态
static XAbstractState* getTargetState(XHistoryState* state);

static void VHistoryState_setParentState(XHistoryState* state, XState* parent);
static void VXHistoryState_deinit(XHistoryState* state);

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
#if SHOWCONTAINERSIZE
    printf("XHistoryState size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

XHistoryState* XHistoryState_create(XHistoryStateType type) {
    XHistoryState* state = (XHistoryState*)XMemory_malloc(sizeof(XHistoryState));
    if (state) {
        XHistoryState_init(state, type);
    }
    return state;
}

void XHistoryState_init(XHistoryState* state, XHistoryStateType type) {
    if (!state) return;
    XAbstractState_init(state, XStateType_History);
    state->m_historyType = type;
    state->m_defaultState = NULL;
    state->m_storedShallow = NULL;
    state->m_storedDeep = XVector_create(sizeof(XAbstractState*));
}

XHistoryStateType XHistoryState_historyType(const XHistoryState* state) {
    return state ? state->m_historyType : XHistoryStateType_Shallow;
}

void XHistoryState_setDefaultState(XHistoryState* state, XAbstractState* defaultState) {
    if (!state || !defaultState) return;

    // 新增：验证默认状态必须是父状态的子状态
    XState* parent = XAbstractState_parentState((XAbstractState*)state);
    if (!parent || !XState_isChild(parent, defaultState)) {
        // XPrintf("默认状态必须是历史状态父状态的子状态！\n");
        return;
    }
    state->m_defaultState = defaultState;
}

XAbstractState* XHistoryState_defaultState(const XHistoryState* state) {
    return state ? state->m_defaultState : NULL;
}

void XHistoryState_storeState(XHistoryState* state, XAbstractState* storedState) {
    if (!state || !storedState) return;
    XState* parent = XAbstractState_parentState((XAbstractState*)state);
    if (!parent) return;

    // 校验：storedState必须是父状态的子状态
    if (!XState_isChild(parent, storedState)) return;

    switch (state->m_historyType) {
    case XHistoryStateType_Shallow:
        // 浅层历史只存储父状态的直接子状态
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

void XHistoryState_storeCurrentState(XHistoryState* state) {
    if (!state) return;
    XState* parent = XAbstractState_parentState((XAbstractState*)state);
    if (!parent) return;

    if (state->m_historyType == XHistoryStateType_Shallow) {
        state->m_storedShallow = getParentActiveChild(parent);
    }
    else {
        XVector_clear_base(state->m_storedDeep);
        collectDeepHistory((XAbstractState*)parent, state->m_storedDeep);
    }
}

// 修正：激活历史状态时返回实际目标状态，由状态机处理过渡
XAbstractState* XHistoryState_activate(XHistoryState* state) {
    if (!state) return NULL;
    return getTargetState(state);
}

// 新增：获取实际要激活的目标状态
static XAbstractState* getTargetState(XHistoryState* state) {
    XState* parent = XAbstractState_parentState((XAbstractState*)state);
    if (!parent) return state->m_defaultState;

    if (state->m_historyType == XHistoryStateType_Shallow) {
        // 浅层历史：优先用存储的直接子状态，否则用默认
        return state->m_storedShallow ? state->m_storedShallow : state->m_defaultState;
    }
    else {
        // 深层历史：如果有存储的状态链，返回链的根（父状态），否则用默认
        if (XVector_size_base(state->m_storedDeep) > 0) {
            return *(XAbstractState**)XVector_at_base(state->m_storedDeep, 0);
        }
        return state->m_defaultState;
    }
}

static void collectDeepHistory(XAbstractState* state, XVector* result) {
    if (!state || !state->m_isRunning) return;
    XVector_push_back_base(result, &state);  // 存储当前活跃状态

    // 只递归处理基本状态的子状态
    if (state->m_type == XStateType_Basic) {
        XState* basic = (XState*)state;
        for (size_t i = 0; i < XState_childCount(basic); i++) {
            collectDeepHistory(XState_child(basic, i), result);
        }
    }
}

static XAbstractState* getParentActiveChild(XState* parent) {
    if (!parent) return NULL;
    for (size_t i = 0; i < XState_childCount(parent); i++) {
        XAbstractState* child = XState_child(parent, i);
        if (child->m_isRunning) return child;
    }
    return NULL;
}

void VHistoryState_setParentState(XHistoryState* state, XState* parent) {
    if (!state || !parent) return;
    ((XAbstractState*)state)->m_parentState = (XAbstractState*)parent;
    XAbstractState_setParentState_base(&state->m_class, (XAbstractState*)parent);
    XAbstractState_setMachine_base(state, XAbstractState_machine((XAbstractState*)parent));

    // 父状态设置后，验证默认状态是否合法
    if (state->m_defaultState && !XState_isChild(parent, state->m_defaultState)) {
        state->m_defaultState = NULL;  // 清除非法默认状态
    }
}

void VXHistoryState_deinit(XHistoryState* state) {
    if (state->m_storedDeep) {
        XVector_delete_base(state->m_storedDeep);
        state->m_storedDeep = NULL;
    }
    XVtableGetFunc(XAbstractState_class_init(), EXClass_Deinit, void(*)(XAbstractState*))(state);
}
