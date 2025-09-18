#include "XFinalState.h"
#include "XStateMachine.h"
#include "XMemory.h"

XFinalState* XFinalState_create() {
    XFinalState* state = (XFinalState*)XMemory_malloc(sizeof(XFinalState));
    if (state) {
        XFinalState_init(state);
    }
    return state;
}

void XFinalState_init(XFinalState* state) {
    if (!state) return;
    XAbstractState_init(&state->parent, XStateType_Final);
}

void XFinalState_destroy(XFinalState* state) {
    if (!state) return;
    XAbstractState_destroy(&state->parent);
    XMemory_free(state);
}

void XFinalState_activate(XFinalState* state, XStateMachine* machine) {
    if (!state || !machine) return;

    // 激活最终状态
    XAbstractState_onEntered(&state->parent, machine);

    // 发送状态机完成信号
    XObject_emitSignal((XObject*)machine, "finished()", NULL);

    // 检查是否是顶层最终状态，如果是则停止状态机
    XState* parent = XAbstractState_parentState(&state->parent);
    if (!parent || !XAbstractState_parentState((XAbstractState*)parent)) {
        XStateMachine_stop(machine);
    }
}