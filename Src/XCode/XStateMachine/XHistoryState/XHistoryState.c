#include "XHistoryState.h"
#include "XState.h"
#include "XStateMachine.h"
#include "XMemory.h"
#include "XVector.h"
// 辅助函数：递归收集深层历史状态（从父状态开始的所有活跃子状态）
static void collectDeepHistory(XAbstractState* state, XVector* result);
// 辅助函数：获取父状态的直接活跃子状态（用于浅层历史）
static XAbstractState* getParentActiveChild(XState* parent);

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

    /* void* table[] =
     {
         VXAbstractState_onEntered,VXAbstractState_onExited
     };

     XVTABLE_ADD_FUNC_LIST_DEFAULT(table);*/
      XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHistoryState_deinit);
      //XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_OnEntered, VXState_onEntered);
      //XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_OnExited, VXState_onExited);
      //XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_SetMachine, VXState_setMachine);
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
    state->m_storedDeep = XVector_create(sizeof(XAbstractState*));  // 初始化动态数组
}

XHistoryStateType XHistoryState_historyType(const XHistoryState* state) {
    return state ? state->m_historyType : XHistoryStateType_Shallow;
}

void XHistoryState_setDefaultState(XHistoryState* state, XAbstractState* defaultState) {
    if (state) {
        state->m_defaultState = defaultState;
    }
}

XAbstractState* XHistoryState_defaultState(const XHistoryState* state) {
    return state ? state->m_defaultState : NULL;
}

void XHistoryState_storeState(XHistoryState* state, XAbstractState* storedState) 
{
    // --------------- 1. 参数合法性校验 ---------------
    if (!state || !storedState) {
       // XPrintf("XHistoryState_storeState: 传入NULL指针！\n");
        return;
    }
    if (!XAbstractState_parentState(state)) {
        //XPrintf("XHistoryState_storeState: 未设置父状态（需先调用XHistoryState_setParentState）！\n");
        return;
    }
    // 校验：storedState必须是当前历史状态父状态的子状态（避免存储无关状态）
    if (XAbstractState_parentState(storedState) != XAbstractState_parentState(state)) {
        //XPrintf("XHistoryState_storeState: storedState不是父状态的子状态，存储无效！\n");
        return;
    }

    // --------------- 2. 按历史类型分支存储 ---------------
    switch (state->m_historyType) {
    case XHistoryStateType_Shallow:
        // 浅层历史：仅存储“基准状态”（父状态的直接子状态）
        // 覆盖旧存储（浅层历史只需要最新的直接子状态）
        state->m_storedShallow = storedState;
        //XPrintf("XHistoryState: 浅层存储状态 %p\n", storedState);
        break;

    case XHistoryStateType_Deep:
        // 深层历史：存储“基准状态 + 其所有活跃子状态”（形成完整状态链）
        // 先清空旧的存储数据，避免历史残留
        XVector_clear_base(state->m_storedDeep);
        // 递归收集活跃子状态链
        collectDeepHistory(storedState, state->m_storedDeep);
        //XPrintf("XHistoryState: 深层存储状态链，共 %zu 个状态\n", XVector_size(state->m_storedDeep));
        break;

    default:
       // XPrintf("XHistoryState_storeState: 未知历史类型！\n");
        break;
    }
}
// 存储当前状态（父状态退出时由XState调用）
void XHistoryState_storeCurrentState(XHistoryState* state)
{
    if (!state || !XAbstractState_parentState(state)) return;
    XState* parent = XAbstractState_parentState(state);

    if (state->m_historyType == XHistoryStateType_Shallow) {
        // 浅层历史：存储父状态的直接活跃子状态（类似Qt的shallow history）
        state->m_storedShallow = getParentActiveChild(parent);
    }
    else {
        // 深层历史：存储所有层级的活跃子状态（类似Qt的deep history）
        XVector_clear_base(state->m_storedDeep);
        collectDeepHistory((XAbstractState*)parent, state->m_storedDeep);
    }
}
// 激活历史状态（恢复存储的状态）
void XHistoryState_activate(XHistoryState* state, XStateMachine* machine)
{
    if (!state || !machine || !XAbstractState_parentState(state)) return;

    // 激活历史状态本身（触发进入回调和信号）
    XAbstractState_onEntered_base(&state->m_class);

    XAbstractState* target = NULL;
    if (state->m_historyType == XHistoryStateType_Shallow) {
        // 浅层恢复：使用存储的直接子状态或默认状态
        target = state->m_storedShallow ? state->m_storedShallow : state->m_defaultState;
        if (target) {
            XAbstractState_onEntered_base(target);  // 激活目标状态
        }
    }
    else {
        // 深层恢复：按层级激活所有存储的状态（从父到子）
        if (XVector_size_base(state->m_storedDeep) > 0) {
            for (size_t i = 0; i < XVector_size_base(state->m_storedDeep); i++) {
                XAbstractState** stored = XVector_at_base(state->m_storedDeep, i);
                XAbstractState_onEntered_base(*stored);
            }
        }
        else if (state->m_defaultState) {
            XAbstractState_onEntered_base(state->m_defaultState);
        }
    }
}

// 辅助函数：递归收集深层历史状态（从父状态开始的所有活跃子状态）
void collectDeepHistory(XAbstractState* state, XVector* result) {
    if (!state || !state->m_isRunning) return;
    XVector_push_back_base(result, &state);  // 存储当前活跃状态
    // 递归处理子状态（仅基本状态有子状态）
    if (state->m_type == XStateType_Basic) {
        XState* basic = (XState*)state;
        for (size_t i = 0; i < XState_childCount(basic); i++) {
            collectDeepHistory(XState_child(basic, i), result);
        }
    }
}

// 辅助函数：获取父状态的直接活跃子状态（用于浅层历史）
XAbstractState* getParentActiveChild(XState* parent) {
    if (!parent) return NULL;
    for (size_t i = 0; i < XState_childCount(parent); i++) {
        XAbstractState* child = XState_child(parent, i);
        if (child->m_isRunning) return child;
    }
    return NULL;  // 无活跃子状态时返回默认
}

void VHistoryState_setParentState(XHistoryState* state, XState* parent)
{
    if (!state || !parent) return;
    ((XAbstractState*)state)->m_parentState = (XAbstractState*)parent;
    XAbstractState_setParentState_base(&state->m_class, (XAbstractState*)parent);
    // 同步状态机关联
    XAbstractState_setMachine_base(state, XAbstractState_machine((XAbstractState*)parent));
}

void VXHistoryState_deinit(XHistoryState* state)
{
    if (state->m_storedDeep)
    {
        XVector_delete_base(state->m_storedDeep);
        state->m_storedDeep = NULL;
    }
    //调用父类释放函数
    XVtableGetFunc(XAbstractState_class_init(), EXClass_Deinit, void(*)(XAbstractState*))(state);
}
