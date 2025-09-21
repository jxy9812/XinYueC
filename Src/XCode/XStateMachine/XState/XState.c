#include "XState.h"
#include "XMemory.h"
#include <string.h>
#define INITIAL_CAPACITY 4
bool XStateMachine_isActive(const XStateMachine* machine, const XAbstractState* state);
void XStateMachine_addActiveState(XStateMachine* machine, XAbstractState* state);
void XStateMachine_removeActiveState(XStateMachine* machine, XAbstractState* state);


XState* XState_create() {
    XState* state = (XState*)XMemory_malloc(sizeof(XState));
    if (state) {
        XState_init(state);
    }
    return state;
}

void XState_init(XState* state) {
    if (!state) return;

    XAbstractState_init(&state->parent, XStateType_Basic);
   

    state->transitionCapacity = INITIAL_CAPACITY;
    state->transitions = (XAbstractTransition**)XMemory_malloc(
        sizeof(XAbstractTransition*) * state->transitionCapacity
    );
    state->transitionCount = 0;

    state->initialState = NULL;
}

void XState_destroy(XState* state) {
    if (!state) return;

    

    // 移除所有转换
    for (size_t i = 0; i < state->transitionCount; i++) {
        XAbstractTransition_destroy(state->transitions[i]);
    }
    XMemory_free(state->transitions);

    XAbstractState_destroy(&state->parent);
    XMemory_free(state);
}





bool XState_addTransition(XState* state, XAbstractTransition* transition) {
    if (!state || !transition) return false;

    // 检查是否已存在
    for (size_t i = 0; i < state->transitionCount; i++) {
        if (state->transitions[i] == transition) {
            return false;
        }
    }

    // 扩容
    if (state->transitionCount >= state->transitionCapacity) {
        size_t newCapacity = state->transitionCapacity * 2;
        XAbstractTransition** newTransitions = (XAbstractTransition**)XMemory_realloc(
            state->transitions, sizeof(XAbstractTransition*) * newCapacity
        );
        if (!newTransitions) return false;

        state->transitions = newTransitions;
        state->transitionCapacity = newCapacity;
    }

    // 添加转换
    state->transitions[state->transitionCount++] = transition;
    XAbstractTransition_setSourceState(transition, (XAbstractState*)state);

    return true;
}

bool XState_removeTransition(XState* state, XAbstractTransition* transition) {
    if (!state || !transition) return false;

    for (size_t i = 0; i < state->transitionCount; i++) {
        if (state->transitions[i] == transition) {
            // 前移元素
            state->transitionCount--;
            for (size_t j = i; j < state->transitionCount; j++) {
                state->transitions[j] = state->transitions[j + 1];
            }

            // 清除转换的源状态
            XAbstractTransition_setSourceState(transition, NULL);
            return true;
        }
    }

    return false;
}

size_t XState_transitionCount(const XState* state) {
    return state ? state->transitionCount : 0;
}

XAbstractTransition* XState_transition(const XState* state, size_t index) {
    if (!state || index >= state->transitionCount) return NULL;
    return state->transitions[index];
}

void XState_setInitialState(XState* state, XAbstractState* initialState) {
    if (state) 
    {
        if (initialState)
            XAbstractState_addState(state,initialState);
        state->initialState = initialState;
    }
}

XAbstractState* XState_initialState(const XState* state) {
    return state ? state->initialState : NULL;
}

void XState_activate(XState* state, XStateMachine* machine) {
    if (!state || !machine) return;

    // 激活当前状态
    XAbstractState_onEntered(&state->parent);

    // 如果有初始子状态，激活它
    if (state->initialState) {
        // 递归激活子状态
        if (state->initialState->type == XStateType_Basic) {
            XState_activate((XState*)state->initialState, machine);
        }
       /* else {
            XAbstractState_onEntered(state->initialState, machine);
        }*/
    }
    //XStateMachine_addActiveState(machine, state);
}

void XState_deactivate(XState* state, XStateMachine* machine) {
    if (!state || !machine) return;

    
    //XStateMachine_removeActiveState(state->machine, state);
    // 失活当前状态
    XAbstractState_onExited(&state->parent);
}