#include "XAbstractTransition.h"
#include "XStateMachine.h"
#include "XState.h"
#include "XMemory.h"

void XAbstractTransition_init(XAbstractTransition* transition, XTransitionType type) {
    if (!transition) return;

    XObject_init(&transition->parent);
    transition->sourceState = NULL;
    transition->targetState = NULL;
    transition->condition = NULL;
    transition->userData = NULL;
    transition->type = type;
}

void XAbstractTransition_destroy(XAbstractTransition* transition) {
    if (!transition) return;

    // 从源状态中移除转换
    if (transition->sourceState && transition->sourceState->type == XStateType_Basic) {
        XState_removeTransition((XState*)transition->sourceState, transition);
    }

    XObject_deinit_base(&transition->parent);
    XMemory_free(transition);
}

XAbstractState* XAbstractTransition_sourceState(const XAbstractTransition* transition) {
    return transition ? transition->sourceState : NULL;
}

void XAbstractTransition_setSourceState(XAbstractTransition* transition, XAbstractState* state) {
    if (!transition) return;

    // 如果之前有源状态，从那里移除
    if (transition->sourceState && transition->sourceState->type == XStateType_Basic) {
        XState_removeTransition((XState*)transition->sourceState, transition);
    }

    transition->sourceState = state;

    // 向新的源状态添加转换
    if (state && state->type == XStateType_Basic) {
        XState_addTransition((XState*)state, transition);
    }
}

XAbstractState* XAbstractTransition_targetState(const XAbstractTransition* transition) {
    return transition ? transition->targetState : NULL;
}

void XAbstractTransition_setTargetState(XAbstractTransition* transition, XAbstractState* state) {
    if (transition) {
        transition->targetState = state;
    }
}

void XAbstractTransition_setCondition(XAbstractTransition* transition, XAbstractTransitionCondition condition) {
    if (transition) {
        transition->condition = condition;
    }
}

bool XAbstractTransition_checkCondition(const XAbstractTransition* transition, const XEvent* event) {
    if (!transition) return false;

    // 如果没有条件，默认返回true
    if (!transition->condition) return true;

    // 调用条件函数
    return transition->condition(transition, event);
}

bool XAbstractTransition_execute(XAbstractTransition* transition, XStateMachine* machine, const XEvent* event) {
    if (!transition || !machine || !transition->sourceState || !transition->targetState) {
        return false;
    }

    // 检查条件是否满足
    if (!XAbstractTransition_checkCondition(transition, event)) {
        return false;
    }

    // 发送转换触发信号
    //XObject_emitSignal(&transition->parent, "triggered()", NULL);

    // 执行状态转换
    return XStateMachine_transition(machine, transition->sourceState, transition->targetState);
}

void XAbstractTransition_setUserData(XAbstractTransition* transition, void* data) {
    if (transition) {
        transition->userData = data;
    }
}

void* XAbstractTransition_userData(const XAbstractTransition* transition) {
    return transition ? transition->userData : NULL;
}