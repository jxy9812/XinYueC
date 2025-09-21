#include "XHistoryState.h"
#include "XState.h"
#include "XStateMachine.h"
#include "XMemory.h"
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
     /* XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXState_deinit);
      XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_OnEntered, VXState_onEntered);
      XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_OnExited, VXState_onExited);
      XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_SetMachine, VXState_setMachine);
      XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_SetParentState, VXState_setParentState);*/
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

    XAbstractState_init(&state->m_class, XStateType_History);
    state->m_historyType = type;
    state->m_defaultState = NULL;
    state->m_storedState = NULL;
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

void XHistoryState_storeState(XHistoryState* state, XAbstractState* storedState) {
    if (state) {
        state->m_storedState = storedState;
    }
}

void XHistoryState_activate(XHistoryState* state, XStateMachine* machine) {
    if (!state || !machine) return;

    // 激活历史状态本身
    XAbstractState_onEntered_base(&state->m_class);

    // 确定要恢复的状态
    XAbstractState* target = state->m_storedState;
    if (!target) {
        target = state->m_defaultState;
    }

    // 如果找到目标状态，激活它
    if (target) {
        if (target->m_type == XStateType_Basic) {
            XState_activate_base((XState*)target);
        }
        else {
            XAbstractState_onEntered_base(target);
        }
    }
}