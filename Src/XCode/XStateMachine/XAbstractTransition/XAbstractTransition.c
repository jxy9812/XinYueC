#include "XAbstractTransition.h"
#include "XStateMachine.h"
#include "XState.h"
#include "XMemory.h"
#include <string.h>
void XAbstractTransition_init(XAbstractTransition* transition, XTransitionType type) {
    if (!transition) return;
    memset(((XClass*)transition)+1,0,sizeof(XAbstractTransition)-sizeof(XClass));
    XClass_init(transition);
    //transition->m_sourceState = NULL;
    //transition->m_targetState = NULL;
    //transition->m_condition = NULL;
    //transition->m_userData = NULL;
    transition->m_type = type;
}

void XAbstractTransition_destroy(XAbstractTransition* transition) {
    if (!transition) return;

    // 从源状态中移除转换
    if (transition->m_sourceState && transition->m_sourceState->m_type == XStateType_Basic) {
        XState_removeTransition((XState*)transition->m_sourceState, transition);
    }

    XObject_deinit_base(&transition->m_class);
    XMemory_free(transition);
}

XAbstractState* XAbstractTransition_sourceState(const XAbstractTransition* transition) {
    return transition ? transition->m_sourceState : NULL;
}

void XAbstractTransition_setSourceState(XAbstractTransition* transition, XAbstractState* state) {
    if (!transition) return;

    // 如果之前有源状态，从那里移除
    if (transition->m_sourceState && transition->m_sourceState->m_type == XStateType_Basic) {
        XState_removeTransition((XState*)transition->m_sourceState, transition);
    }

    transition->m_sourceState = state;

    // 向新的源状态添加转换
    if (state && state->m_type == XStateType_Basic) {
        XState_addTransition((XState*)state, transition);
    }
}

XAbstractState* XAbstractTransition_targetState(const XAbstractTransition* transition) {
    return transition ? transition->m_targetState : NULL;
}

void XAbstractTransition_setTargetState(XAbstractTransition* transition, XAbstractState* state) {
    if (transition) {
        transition->m_targetState = state;
    }
}

void XAbstractTransition_setCondition(XAbstractTransition* transition, XAbstractTransitionCondition condition) {
    if (transition) {
        transition->m_condition = condition;
    }
}

bool XAbstractTransition_checkCondition(const XAbstractTransition* transition, const XEvent* event) {
    if (!transition) return false;

    // 如果没有条件，默认返回true
    if (!transition->m_condition) return true;

    // 调用条件函数
    return transition->m_condition(transition, event);
}

bool XAbstractTransition_execute(XAbstractTransition* transition, XStateMachine* machine, const XEvent* event) {
    if (!transition || !machine || !transition->m_sourceState || !transition->m_targetState) {
        return false;
    }

    // 检查条件是否满足
    if (!XAbstractTransition_checkCondition(transition, event)) {
        return false;
    }

    // 发送转换触发信号
    //XObject_emitSignal(&transition->m_class, "triggered()", NULL);

    // 执行状态转换
    return XStateMachine_transition(machine, transition->m_sourceState, transition->m_targetState);
}

void XAbstractTransition_setUserData(XAbstractTransition* transition, void* data) {
    if (transition) {
        transition->m_userData = data;
    }
}

void* XAbstractTransition_userData(const XAbstractTransition* transition) {
    return transition ? transition->m_userData : NULL;
}