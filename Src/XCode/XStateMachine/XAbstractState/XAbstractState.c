#include "XAbstractState.h"
#include "XStateMachine.h"
#include "XState.h"
#include "XStack.h"
#include "XMemory.h"
#define INITIAL_CAPACITY 4
bool XStateMachine_isActive(const XStateMachine* machine, const XAbstractState* state);
void XStateMachine_addActiveState(XStateMachine* machine, XAbstractState* state);
void XStateMachine_removeActiveState(XStateMachine* machine, XAbstractState* state);

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

    XClass_init(&state->m_class);
    state->type = type;
    state->parentState = NULL;
    state->machine = NULL;
    state->isRunning = false;
    state->userData = NULL;
    state->privateData = NULL;
    state->childCapacity = 0;
    state->childStates = 0;
    state->childCount = 0;

    // 初始化私有数据
    XAbstractState_private_init(state);
}

void XAbstractState_destroy(XAbstractState* state) {
    if (!state) return;
    // 移除所有子状态
    for (size_t i = 0; i < state->childCount; i++) {
        XAbstractState_destroy(state->childStates[i]);
    }
    XMemory_free(state->childStates);
    // 释放私有数据
    XMemory_free(state->privateData);
    state->privateData = NULL;

    XObject_deinit_base(state);
}
bool XAbstractState_addState(XAbstractState* state, XAbstractState* child)
{
    if (!state || !child) return false;
    if (state->machine != child->machine && child->machine != NULL)return false;
    if (state->childCapacity == 0)
    {//初始化
        state->childCapacity = INITIAL_CAPACITY;
        state->childStates = (XAbstractState**)XMemory_malloc(
        sizeof(XAbstractState*) * state->childCapacity);
    }
    else 
    {
        // 检查是否已存在
        for (size_t i = 0; i < state->childCount; i++) {
            if (state->childStates[i] == child) {
                return false;
            }
        }
    }
   

    // 扩容
    if (state->childCount >= state->childCapacity) {
        size_t newCapacity = state->childCapacity * 2;
        XAbstractState** newChildren = (XAbstractState**)XMemory_realloc(
            state->childStates, sizeof(XAbstractState*) * newCapacity
        );
        if (!newChildren) return false;

        state->childStates = newChildren;
        state->childCapacity = newCapacity;
    }

    // 添加子状态并设置父状态
    state->childStates[state->childCount++] = child;
    XAbstractState_setParentState(child, state);
    child->machine = state->machine;
    return true;
}

bool XAbstractState_removeState(XAbstractState* state, XAbstractState* child) 
{
    if (!state || !child) return false;

    for (size_t i = 0; i < state->childCount; i++) {
        if (state->childStates[i] == child) {
            // 前移元素
            state->childCount--;
            for (size_t j = i; j < state->childCount; j++) {
                state->childStates[j] = state->childStates[j + 1];
            }

            // 清除子状态的父状态
            XAbstractState_setParentState(child, NULL);
            return true;
        }
    }

    return false;
}

size_t XAbstractState_childCount(const XAbstractState* state) 
{
    return state ? state->childCount : 0;
}

XAbstractState* XAbstractState_child(const XAbstractState* state, size_t index) 
{
    if (!state || index >= state->childCount) return NULL;
    return state->childStates[index];
}
XStateType XAbstractState_type(const XAbstractState* state) {
    return state ? state->type : XStateType_Basic;
}

XState* XAbstractState_parentState(const XAbstractState* state) {
    return state ? state->parentState : NULL;
}

void XAbstractState_setParentState(XAbstractState* state, XAbstractState* parent) {
    if (state) 
    {
        if (state->parentState == parent)
            return;
        if (state->parentState != NULL)
        {//先从原来的父状态删除
            XAbstractState_removeState(state->parentState,state);
        }
        state->parentState = parent;
        XAbstractState_setMachine(state,parent->machine);
    }
}

void XAbstractState_setMachine(XAbstractState* state, XStateMachine* machine)
{
    if (!state)
        return;
    //设置子状态所属的状态机
    XStack* stack = XStack_create(sizeof(XAbstractState*));
    XStack_push_base(stack, &state);
    while (!XStack_isEmpty_base(stack))
    {
        XAbstractState* current = XStack_Top_Base(stack, XAbstractState*);
        XStack_pop_base(stack);
        for (size_t i = 0; i < current->childCount; i++)
        {
            XStack_push_base(stack, current->childStates + i);
        }
        current->machine = machine;
    }
    XStack_delete_base(stack);
    state->machine = machine;
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

void XAbstractState_onEntered(XAbstractState* state) {
    if (!state || state->isRunning||!state->machine|| !XStateMachine_isRunning(state->machine)) return;

    state->isRunning = true;

    // 触发进入回调
    XAbstractStatePrivate* d = XAbstractState_private(state);
    if (d->enteredCallback) {
        d->enteredCallback(state);
    }
    XStateMachine_addActiveState(state->machine, state);
    // 发送状态进入信号
    XStateMachine_entered_signal(state->machine,state);
}

void XAbstractState_onExited(XAbstractState* state) {
    if (!state || !state->isRunning || !state->machine || !XStateMachine_isRunning(state->machine)) return;
    // 失活所有子状态
    for (size_t i = 0; i < state->childCount; i++) {
        XAbstractState* child = state->childStates[i];
        if (child->isRunning) {
           /* if (child->type == XStateType_Basic) {
                XAbstractState_onExited((XState*)child);
            }
            else */
            {
                XAbstractState_onExited(child);
            }
        }
    }
    // 触发退出回调
    XAbstractStatePrivate* d = XAbstractState_private(state);
    if (d->exitedCallback) {
        d->exitedCallback(state);
    }

    state->isRunning = false;
    XStateMachine_removeActiveState(state->machine, state);
    // 发送状态退出信号
    XStateMachine_exited_signal(state->machine,state);
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