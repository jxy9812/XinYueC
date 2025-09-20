#include "XStateMachine.h"
#include "XState.h"
#include "XFinalState.h"
#include "XHistoryState.h"
#include "XEventTransition.h"
#include "XMemory.h"
#include <string.h>

#define INITIAL_STATE_CAPACITY 4

// 私有函数声明
static void XStateMachine_enterState(XStateMachine* machine, XAbstractState* state);
static void XStateMachine_exitState(XStateMachine* machine, XAbstractState* state);
static bool XStateMachine_isActive(const XStateMachine* machine, const XAbstractState* state);
static void XStateMachine_addActiveState(XStateMachine* machine, XAbstractState* state);
static void XStateMachine_removeActiveState(XStateMachine* machine, XAbstractState* state);

XStateMachine* XStateMachine_create() {
    XStateMachine* machine = (XStateMachine*)XMemory_malloc(sizeof(XStateMachine));
    if (machine) {
        XStateMachine_init(machine);
    }
    return machine;
}

void XStateMachine_init(XStateMachine* machine) {
    if (!machine) return;

    XObject_init(&machine->parent);
    machine->initialState = NULL;
    machine->activeStateCapacity = INITIAL_STATE_CAPACITY;
    machine->activeStates = (XAbstractState**)XMemory_malloc(
        sizeof(XAbstractState*) * machine->activeStateCapacity
    );
    machine->activeStateCount = 0;
    machine->status = XStateMachineStopped;
    machine->userData = NULL;
}

void XStateMachine_destroy(XStateMachine* machine) {
    if (!machine) return;

    // 停止状态机
    XStateMachine_stop(machine);

    // 释放激活状态列表
    XMemory_free(machine->activeStates);

    XObject_deinit_base(&machine->parent);
    XMemory_free(machine);
}

void XStateMachine_setInitialState(XStateMachine* machine, XAbstractState* state) {
    if (machine) {
        machine->initialState = state;
    }
}

XAbstractState* XStateMachine_initialState(const XStateMachine* machine) {
    return machine ? machine->initialState : NULL;
}

bool XStateMachine_addState(XStateMachine* machine, XAbstractState* state) {
    if (!machine || !state) return false;

    // 对于顶层状态（没有父状态），设置其所属状态机
    if (!XAbstractState_parentState(state)) {
        // 这里不需要实际存储状态，状态通过父状态关系管理
        ((XAbstractState*)state)->machine = machine;
    }

    return true;
}

bool XStateMachine_removeState(XStateMachine* machine, XAbstractState* state) {
    if (!machine || !state) return false;

    // 如果状态正在运行，先退出
    if (XStateMachine_isActive(machine, state)) {
        XStateMachine_exitState(machine, state);
    }

    // 清除状态机引用
    if (((XAbstractState*)state)->machine == machine) {
        ((XAbstractState*)state)->machine = NULL;
    }

    return true;
}

bool XStateMachine_start(XStateMachine* machine) {
    if (!machine || machine->status != XStateMachineStopped || !machine->initialState) {
        return false;
    }

    // 清空当前激活状态
    for (size_t i = 0; i < machine->activeStateCount; i++) {
        XStateMachine_exitState(machine, machine->activeStates[i]);
    }
    machine->activeStateCount = 0;

    // 进入初始状态
    XStateMachine_enterState(machine, machine->initialState);

    // 设置状态机为运行状态
    machine->status = XStateMachineRunning;

    // 发送启动信号
    //XObject_emitSignal(&machine->m_class, "started()", NULL);

    return true;
}

void XStateMachine_stop(XStateMachine* machine) {
    if (!machine || machine->status == XStateMachineStopped) return;

    // 退出所有激活状态
    for (size_t i = 0; i < machine->activeStateCount; i++) {
        XStateMachine_exitState(machine, machine->activeStates[i]);
    }
    machine->activeStateCount = 0;

    // 设置状态机为停止状态
    machine->status = XStateMachineStopped;

    // 发送停止信号
    //XObject_emitSignal(&machine->m_class, "stopped()", NULL);
}

void XStateMachine_pause(XStateMachine* machine) {
    if (machine && machine->status == XStateMachineRunning) {
        machine->status = XStateMachinePaused;
       // XObject_emitSignal(&machine->m_class, "paused()", NULL);
    }
}

void XStateMachine_resume(XStateMachine* machine) {
    if (machine && machine->status == XStateMachinePaused) {
        machine->status = XStateMachineRunning;
        //XObject_emitSignal(&machine->m_class, "resumed()", NULL);
    }
}

XStateMachineStatus XStateMachine_status(const XStateMachine* machine) {
    return machine ? machine->status : XStateMachineStopped;
}

bool XStateMachine_handleEvent(XStateMachine* machine, const XEvent* event) {
    if (!machine || !event || machine->status != XStateMachineRunning) {
        return false;
    }

    // 保存当前激活状态的快照，防止处理过程中状态变化影响遍历
    XAbstractState** snapshot = (XAbstractState**)XMemory_malloc(
        sizeof(XAbstractState*) * machine->activeStateCount
    );
    if (!snapshot) return false;

    memcpy(snapshot, machine->activeStates, sizeof(XAbstractState*) * machine->activeStateCount);
    size_t snapshotCount = machine->activeStateCount;
    bool eventHandled = false;

    // 处理事件：检查所有激活状态的转换
    for (size_t i = 0; i < snapshotCount && !eventHandled; i++) {
        XAbstractState* state = snapshot[i];

        // 仅处理基本状态的转换
        if (state->type == XStateType_Basic) {
            XState* basicState = (XState*)state;

            // 检查所有转换
            for (size_t j = 0; j < XState_transitionCount(basicState) && !eventHandled; j++) {
                XAbstractTransition* transition = XState_transition(basicState, j);

                // 事件转换特殊处理
                //if (transition->m_class.type == XEventTransitionType)
                {
                    eventHandled = XEventTransition_processEvent(
                        (XEventTransition*)transition, machine, event
                    );
                }
                // 其他类型转换可以在这里添加处理
            }
        }
    }

    XMemory_free(snapshot);
    return eventHandled;
}

bool XStateMachine_transition(XStateMachine* machine, XAbstractState* source, XAbstractState* target) {
    if (!machine || !source || !target || machine->status != XStateMachineRunning) {
        return false;
    }

    // 退出源状态及其子状态
    XStateMachine_exitState(machine, source);

    // 进入目标状态及其所需的父状态
    XStateMachine_enterState(machine, target);

    // 发送转换完成信号
    //XObject_emitSignal(&machine->m_class, "transitioned()", NULL);

    return true;
}

size_t XStateMachine_activeStateCount(const XStateMachine* machine) {
    return machine ? machine->activeStateCount : 0;
}

XAbstractState* XStateMachine_activeState(const XStateMachine* machine, size_t index) {
    if (!machine || index >= machine->activeStateCount) return NULL;
    return machine->activeStates[index];
}

bool XStateMachine_isRunning(const XStateMachine* machine) {
    return machine && machine->status == XStateMachineRunning;
}

bool XStateMachine_isFinished(const XStateMachine* machine) {
    if (!machine || machine->activeStateCount == 0) return false;

    // 检查所有激活状态是否都是最终状态
    for (size_t i = 0; i < machine->activeStateCount; i++) {
        if (machine->activeStates[i]->type != XStateType_Final) {
            return false;
        }
    }
    return true;
}

void XStateMachine_setUserData(XStateMachine* machine, void* data) {
    if (machine) {
        machine->userData = data;
    }
}

void* XStateMachine_userData(const XStateMachine* machine) {
    return machine ? machine->userData : NULL;
}

// 私有函数实现
static bool XStateMachine_isActive(const XStateMachine* machine, const XAbstractState* state) {
    if (!machine || !state) return false;

    for (size_t i = 0; i < machine->activeStateCount; i++) {
        if (machine->activeStates[i] == state) {
            return true;
        }
    }
    return false;
}

static void XStateMachine_addActiveState(XStateMachine* machine, XAbstractState* state) {
    if (!machine || !state || XStateMachine_isActive(machine, state)) {
        return;
    }

    // 扩容
    if (machine->activeStateCount >= machine->activeStateCapacity) {
        size_t newCapacity = machine->activeStateCapacity * 2;
        XAbstractState** newStates = (XAbstractState**)XMemory_realloc(
            machine->activeStates, sizeof(XAbstractState*) * newCapacity
        );
        if (!newStates) return;

        machine->activeStates = newStates;
        machine->activeStateCapacity = newCapacity;
    }

    machine->activeStates[machine->activeStateCount++] = state;
}

static void XStateMachine_removeActiveState(XStateMachine* machine, XAbstractState* state) {
    if (!machine || !state) return;

    for (size_t i = 0; i < machine->activeStateCount; i++) {
        if (machine->activeStates[i] == state) {
            // 前移元素
            machine->activeStateCount--;
            for (size_t j = i; j < machine->activeStateCount; j++) {
                machine->activeStates[j] = machine->activeStates[j + 1];
            }
            break;
        }
    }
}

static void XStateMachine_enterState(XStateMachine* machine, XAbstractState* state) {
    if (!machine || !state || XStateMachine_isActive(machine, state)) {
        return;
    }

    // 对于非顶层状态，先进入其父状态
    XState* parent = XAbstractState_parentState(state);
    if (parent && !XStateMachine_isActive(machine, (XAbstractState*)parent)) {
        XStateMachine_enterState(machine, (XAbstractState*)parent);
    }

    // 特殊处理不同类型的状态
    switch (state->type) {
    case XStateType_Basic:
        XState_activate((XState*)state, machine);
        break;
    case XStateType_Final:
        XFinalState_activate((XFinalState*)state, machine);
        break;
    case XStateType_History:
        XHistoryState_activate((XHistoryState*)state, machine);
        break;
    case XStateType_Parallel:
        // 并行状态处理（类似于基本状态，但需要激活所有子状态）
        XState_activate((XState*)state, machine);
        break;
    }

    // 添加到激活状态列表
    XStateMachine_addActiveState(machine, state);
}

static void XStateMachine_exitState(XStateMachine* machine, XAbstractState* state) {
    if (!machine || !state || !XStateMachine_isActive(machine, state)) {
        return;
    }

    // 退出状态
    XAbstractState_onExited(state, machine);

    // 对于历史状态，存储当前子状态
    if (state->parentState && ((XAbstractState*)state->parentState)->type == XStateType_History) {
        XHistoryState_storeState((XHistoryState*)state->parentState, state);
    }

    // 从激活状态列表移除
    XStateMachine_removeActiveState(machine, state);

    // 退出子状态（历史状态和最终状态没有子状态）
    if (state->type == XStateType_Basic || state->type == XStateType_Parallel) {
        XState* basicState = (XState*)state;
        for (size_t i = 0; i < XState_childCount(basicState); i++) {
            XAbstractState* child = XState_child(basicState, i);
            XStateMachine_exitState(machine, child);
        }
    }
}