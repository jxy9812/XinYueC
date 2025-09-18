#include "XAbstractState.h"
#include "XStateMachine.h"
#include "XState.h"
#include "XMemory.h"

// 状态私有数据结构
typedef struct {
    XStateEnteredCallback enteredCallback;
    XStateExitedCallback exitedCallback;
} XAbstractStatePrivate;

static void XAbstractState_private_init(XAbstractState* state) {
    XAbstractStatePrivate* d = XMemory_malloc(sizeof(XAbstractStatePrivate));
    d->enteredCallback = NULL;
    d->exitedCallback = NULL;
    state->privateData = d;  // 存储私有数据
}

static XAbstractStatePrivate* XAbstractState_private(const XAbstractState* state) {
    return (XAbstractStatePrivate*)state->privateData;
}

void XAbstractState_init(XAbstractState* state, XStateType type) {
    if (!state) return;

    XObject_init(&state->parent);
    state->type = type;
    state->parentState = NULL;
    state->machine = NULL;
    state->isRunning = false;
    state->userData = NULL;
    state->privateData = NULL;

    // 初始化私有数据
    XAbstractState_private_init(state);
}

void XAbstractState_destroy(XAbstractState* state) {
    if (!state) return;

    // 释放私有数据
    XMemory_free(state->privateData);
    state->privateData = NULL;

    XObject_deinit_base(&state->parent);
}

XStateType XAbstractState_type(const XAbstractState* state) {
    return state ? state->type : XStateType_Basic;
}

XState* XAbstractState_parentState(const XAbstractState* state) {
    return state ? state->parentState : NULL;
}

void XAbstractState_setParentState(XAbstractState* state, XState* parent) {
    if (state) {
        state->parentState = parent;
    }
}

XStateMachine* XAbstractState_machine(const XAbstractState* state) {
    return state ? state->machine : NULL;
}

bool XAbstractState_isRunning(const XAbstractState* state) {
    return state ? state->isRunning : false;
}

void XAbstractState_setUserData(XAbstractState* state, void* data) {
    if (state) {
        state->userData = data;  // 仅设置用户数据，不影响私有数据
    }
}

void* XAbstractState_userData(const XAbstractState* state) {
    return state ? state->userData : NULL;
}

void XAbstractState_onEntered(XAbstractState* state, XStateMachine* machine) {
    if (!state || !machine) return;

    state->machine = machine;
    state->isRunning = true;

    // 触发进入回调
    XAbstractStatePrivate* d = XAbstractState_private(state);
    if (d->enteredCallback) {
        d->enteredCallback(state, machine);
    }

    // 发送状态进入信号
    XObject_emitSignal(&state->parent, "entered()", NULL);
}

void XAbstractState_onExited(XAbstractState* state, XStateMachine* machine) {
    if (!state || !machine) return;

    // 触发退出回调
    XAbstractStatePrivate* d = XAbstractState_private(state);
    if (d->exitedCallback) {
        d->exitedCallback(state, machine);
    }

    state->isRunning = false;

    // 发送状态退出信号
    XObject_emitSignal(&state->parent, "exited()", NULL);
}

void XAbstractState_setEnteredCallback(XAbstractState* state, XStateEnteredCallback callback) {
    if (state) {
        XAbstractStatePrivate* d = XAbstractState_private(state);
        d->enteredCallback = callback;
    }
}

void XAbstractState_setExitedCallback(XAbstractState* state, XStateExitedCallback callback) {
    if (state) {
        XAbstractStatePrivate* d = XAbstractState_private(state);
        d->exitedCallback = callback;
    }
}