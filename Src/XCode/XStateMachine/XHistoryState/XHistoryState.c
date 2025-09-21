#include "XHistoryState.h"
#include "XState.h"
#include "XStateMachine.h"
#include "XMemory.h"

XHistoryState* XHistoryState_create(XHistoryStateType type) {
    XHistoryState* state = (XHistoryState*)XMemory_malloc(sizeof(XHistoryState));
    if (state) {
        XHistoryState_init(state, type);
    }
    return state;
}

void XHistoryState_init(XHistoryState* state, XHistoryStateType type) {
    if (!state) return;

    XAbstractState_init(&state->parent, XStateType_History);
    state->historyType = type;
    state->defaultState = NULL;
    state->storedState = NULL;
}

void XHistoryState_destroy(XHistoryState* state) {
    if (!state) return;
    XAbstractState_delete_base(&state->parent);
    XMemory_free(state);
}

XHistoryStateType XHistoryState_historyType(const XHistoryState* state) {
    return state ? state->historyType : XHistoryStateType_Shallow;
}

void XHistoryState_setDefaultState(XHistoryState* state, XAbstractState* defaultState) {
    if (state) {
        state->defaultState = defaultState;
    }
}

XAbstractState* XHistoryState_defaultState(const XHistoryState* state) {
    return state ? state->defaultState : NULL;
}

void XHistoryState_storeState(XHistoryState* state, XAbstractState* storedState) {
    if (state) {
        state->storedState = storedState;
    }
}

void XHistoryState_activate(XHistoryState* state, XStateMachine* machine) {
    if (!state || !machine) return;

    // 激活历史状态本身
    XAbstractState_onEntered_base(&state->parent);

    // 确定要恢复的状态
    XAbstractState* target = state->storedState;
    if (!target) {
        target = state->defaultState;
    }

    // 如果找到目标状态，激活它
    if (target) {
        if (target->type == XStateType_Basic) {
            XState_activate_base((XState*)target);
        }
        else {
            XAbstractState_onEntered_base(target);
        }
    }
}