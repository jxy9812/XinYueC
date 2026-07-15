#include "XStateMachine.h"
#include "XState.h"
#include "XFinalState.h"
#include "XHistoryState.h"
#include "XEventTransition.h"
#include "XMemory.h"
#include "XStack.h"
#include <string.h>

#define INITIAL_STATE_CAPACITY 4

// 私有函数声明
void XStateMachine_enterState(XStateMachine* machine, XAbstractState* state);
void XStateMachine_exitState(XStateMachine* machine, XAbstractState* state);
bool XStateMachine_isActive(const XStateMachine* machine, const XAbstractState* state);
void XStateMachine_addActiveState(XStateMachine* machine, XAbstractState* state);
void XStateMachine_removeActiveState(XStateMachine* machine, XAbstractState* state);
static void VXStateMachine_deinit(XStateMachine* machine);
XVtable* XStateMachine_class_init()
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XObject))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_XCLASS(XObject);

    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStateMachine_deinit);
#if SHOWCONTAINERSIZE
    printf("XStateMachine size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

XStateMachine* XStateMachine_create() {
    XStateMachine* machine = (XStateMachine*)XMalloc_System(sizeof(XStateMachine));
    if (machine) {
        XStateMachine_init(machine);
        Set_Class_MemoryFree(machine, XFree_System);
    }
    return machine;
}

void XStateMachine_init(XStateMachine* machine) {
    if (!machine) return;
    memset(((XObject*)machine) + 1, 0, sizeof(XStateMachine) - sizeof(XObject));
    XObject_init(machine);
    XClassGetVtable(machine) = XStateMachine_class_init();
    machine->m_initialState = NULL;
    machine->m_activeStateCapacity = INITIAL_STATE_CAPACITY;
    machine->m_activeStates = (XAbstractState**)XMalloc_System(
        sizeof(XAbstractState*) * machine->m_activeStateCapacity
    );
    machine->m_activeStateCount = 0;
    machine->m_status = XStateMachineStopped;
    machine->m_userData = NULL;
    machine->m_error = XStateMachineNoError;
    machine->m_errorString = NULL;
    machine->m_globalRestorePolicy = XStateMachineGlobalRestorePolicy;
    machine->m_animated = false;
}

void XStateMachine_setInitialState(XStateMachine* machine, XAbstractState* state) {
    if (machine) {
        machine->m_initialState = state;
    }
}

XAbstractState* XStateMachine_initialState(const XStateMachine* machine) {
    return machine ? machine->m_initialState : NULL;
}

bool XStateMachine_addState(XStateMachine* machine, XAbstractState* state) {
    if (!machine || !state) return false;

    // 对于顶层状态（没有父状态），设置其所属状态机
    if (!XAbstractState_parentState(state)) 
    {
        XAbstractState_setMachine_base(state, machine);
        if (machine->m_initialState == NULL)
            machine->m_initialState = state;
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
    if (((XAbstractState*)state)->m_machine == machine) {
        ((XAbstractState*)state)->m_machine = NULL;
    }

    return true;
}

bool XStateMachine_start(XStateMachine* machine) {
    if (!machine || machine->m_status != XStateMachineStopped || !machine->m_initialState) {
        return false;
    }
    // 设置状态机为运行状态
    machine->m_status = XStateMachineRunning;

    // 清空当前激活状态
    for (size_t i = 0; i < machine->m_activeStateCount; i++) {
        XStateMachine_exitState(machine, machine->m_activeStates[i]);
    }
    machine->m_activeStateCount = 0;

    // 清除之前的错误
    machine->m_error = XStateMachineNoError;
    machine->m_errorString = NULL;

    // 发送 runningChanged 信号 (Qt 6.8)
    XStateMachine_runningChanged_signal(machine, true);

    // 进入初始状态
    XStateMachine_enterState(machine, machine->m_initialState);

    // 发送启动信号
    XStateMachine_start_signal(machine);

    return true;
}

void XStateMachine_stop(XStateMachine* machine) {
    if (!machine || machine->m_status == XStateMachineStopped) return;

    bool wasRunning = (machine->m_status == XStateMachineRunning);

    // 退出所有激活状态
    for (size_t i = 0; i < machine->m_activeStateCount; i++) {
        XStateMachine_exitState(machine, machine->m_activeStates[i]);
    }
    machine->m_activeStateCount = 0;

    // 设置状态机为停止状态
    machine->m_status = XStateMachineStopped;

    // 发送 runningChanged 信号 (Qt 6.8)
    if (wasRunning) {
        XStateMachine_runningChanged_signal(machine, false);
    }

    // 发送停止信号
    XStateMachine_stop_signal(machine);
}

void XStateMachine_pause(XStateMachine* machine) {
    if (machine && machine->m_status == XStateMachineRunning) {
        machine->m_status = XStateMachinePaused;
        XStateMachine_runningChanged_signal(machine, false);
        XStateMachine_pause_signal(machine);
    }
}

void XStateMachine_resume(XStateMachine* machine) {
    if (machine && machine->m_status == XStateMachinePaused) {
        machine->m_status = XStateMachineRunning;
        XStateMachine_runningChanged_signal(machine, true);
        XStateMachine_resume_signal(machine);
    }
}

XStateMachineStatus XStateMachine_status(const XStateMachine* machine) {
    return machine ? machine->m_status : XStateMachineStopped;
}

void XStateMachine_handleEventCB(const XEvent* event) {
    if (!event) {
        return ;
    }
    XStateMachine* machine = NULL;
    if(!machine || machine->m_status != XStateMachineRunning) return ;

    // 保存当前激活状态的快照，防止处理过程中状态变化影响遍历
    XAbstractState** snapshot = (XAbstractState**)XMalloc_System(
        sizeof(XAbstractState*) * machine->m_activeStateCount
    );
    if (!snapshot) return ;

    memcpy(snapshot, machine->m_activeStates, sizeof(XAbstractState*) * machine->m_activeStateCount);
    size_t snapshotCount = machine->m_activeStateCount;
    bool eventHandled = false;

    // 处理事件：检查所有激活状态的转换
    for (size_t i = 0; i < snapshotCount && !eventHandled; i++) {
        XAbstractState* state = snapshot[i];

        // 仅处理基本状态的转换
        if (state->m_type == XStateType_Basic) {
            XState* basicState = (XState*)state;

            // 检查所有转换
            for (size_t j = 0; j < XState_transitionCount(basicState) && !eventHandled; j++) {
                XAbstractTransition* transition = XState_transition(basicState, j);

                // 事件转换特殊处理
                if (transition->m_type == XEventTransitionType)
                {
                    eventHandled = XEventTransition_processEvent(
                        (XEventTransition*)transition, machine, event
                    );
                }
                // 其他类型转换可以在这里添加处理
            }
        }
    }

    XFree_System(snapshot);
}

bool XStateMachine_transition(XStateMachine* machine, XAbstractState* source, XAbstractState* target) {
    if (!machine || !source || !target) return false;

    // 退出源状态
    XStateMachine_exitState(machine, source);

    // 进入目标状态
    XStateMachine_enterState(machine, target);

    // 检查是否到达最终状态 (Qt 6.8: finished signal)
    if (target->m_type == XStateType_Final) {
        // 检查是否是顶层最终状态
        XState* parent = XAbstractState_parentState(target);
        if (!parent || !XAbstractState_parentState((XAbstractState*)parent)) {
            XStateMachine_finished_signal(machine);
            XStateMachine_stop(machine);
        } else {
            // 子状态中的最终状态 -> 发送父状态的 finished 信号
            if (parent->m_class.m_type == XStateType_Basic) {
                XState_finished_signal((XState*)parent);
            }
        }
    }

    return true;
}

size_t XStateMachine_activeStateCount(const XStateMachine* machine) {
    return machine ? machine->m_activeStateCount : 0;
}

XAbstractState* XStateMachine_activeState(const XStateMachine* machine, size_t index) {
    if (!machine || index >= machine->m_activeStateCount) return NULL;
    return machine->m_activeStates[index];
}

bool XStateMachine_isRunning(const XStateMachine* machine) {
    return machine && machine->m_status == XStateMachineRunning;
}

bool XStateMachine_isFinished(const XStateMachine* machine) {
    return machine && machine->m_status == XStateMachineStopped;
}

void XStateMachine_setUserData(XStateMachine* machine, void* data) {
    if (machine) {
        machine->m_userData = data;
    }
}

void* XStateMachine_userData(const XStateMachine* machine) {
    return machine ? machine->m_userData : NULL;
}

// Qt 6.8: 错误处理
XStateMachine_Error XStateMachine_error(const XStateMachine* machine) {
    return machine ? machine->m_error : XStateMachineNoError;
}

const char* XStateMachine_errorString(const XStateMachine* machine) {
    return machine ? machine->m_errorString : NULL;
}

void XStateMachine_clearError(XStateMachine* machine) {
    if (machine) {
        machine->m_error = XStateMachineNoError;
        machine->m_errorString = NULL;
    }
}

void XStateMachine_setError(XStateMachine* machine, XStateMachine_Error error, const char* errorString) {
    if (machine) {
        machine->m_error = error;
        machine->m_errorString = errorString;
        XStateMachine_error_signal(machine, error, errorString);
    }
}

// Qt 6.8: globalRestorePolicy
XStateMachine_GlobalRestorePolicy XStateMachine_globalRestorePolicy(const XStateMachine* machine) {
    return machine ? machine->m_globalRestorePolicy : XStateMachineGlobalRestorePolicy;
}

void XStateMachine_setGlobalRestorePolicy(XStateMachine* machine, XStateMachine_GlobalRestorePolicy policy) {
    if (machine) {
        machine->m_globalRestorePolicy = policy;
    }
}

// Qt 6.8: animated
bool XStateMachine_isAnimated(const XStateMachine* machine) {
    return machine ? machine->m_animated : false;
}

void XStateMachine_setAnimated(XStateMachine* machine, bool enabled) {
    if (machine) {
        machine->m_animated = enabled;
    }
}

/*                                                          信号                                                          */    
// 状态进入信号 — 修复: 传递 NULL 作为 args，避免裸指针被当作 XVarList 释放
void* XStateMachine_entered_signal(XStateMachine* machine, XAbstractState* state)
{
    if (machine)
        XObject_emitSignal(machine, XStateMachine_entered_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return XStateMachine_entered_signal;
}

// 状态退出信号 — 修复: 传递 NULL 作为 args
void* XStateMachine_exited_signal(XStateMachine* machine, XAbstractState* state)
{
    if (machine)
        XObject_emitSignal(machine, XStateMachine_exited_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return XStateMachine_exited_signal;
}

void* XStateMachine_start_signal(XStateMachine* machine)
{
    if (machine)
        XObject_emitSignal(machine, XStateMachine_start_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return XStateMachine_start_signal;
}

void* XStateMachine_stop_signal(XStateMachine* machine)
{
    if (machine)
        XObject_emitSignal(machine, XStateMachine_stop_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return XStateMachine_stop_signal;
}

void* XStateMachine_pause_signal(XStateMachine* machine)
{
    if (machine)
        XObject_emitSignal(machine, XStateMachine_pause_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return XStateMachine_pause_signal;
}

void* XStateMachine_resume_signal(XStateMachine* machine)
{
    if (machine)
        XObject_emitSignal(machine, XStateMachine_resume_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return XStateMachine_resume_signal;
}

// Qt 6.8: finished 信号
void* XStateMachine_finished_signal(XStateMachine* machine)
{
    if (machine)
        XObject_emitSignal(machine, XStateMachine_finished_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return XStateMachine_finished_signal;
}

// Qt 6.8: runningChanged 信号
void* XStateMachine_runningChanged_signal(XStateMachine* machine, bool running)
{
    if (machine)
        XObject_emitSignal(machine, XStateMachine_runningChanged_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return XStateMachine_runningChanged_signal;
}

// Qt 6.8: error 信号
void* XStateMachine_error_signal(XStateMachine* machine, XStateMachine_Error error, const char* errorString)
{
    if (machine)
        XObject_emitSignal(machine, XStateMachine_error_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return XStateMachine_error_signal;
}

// 私有函数实现
bool XStateMachine_isActive(const XStateMachine* machine, const XAbstractState* state) {
    if (!machine || !state) return false;

    for (size_t i = 0; i < machine->m_activeStateCount; i++) {
        if (machine->m_activeStates[i] == state) {
            return true;
        }
    }
    return false;
}

void XStateMachine_addActiveState(XStateMachine* machine, XAbstractState* state) {
    if (!machine || !state || XStateMachine_isActive(machine, state)) {
        return;
    }

    // 扩容
    if (machine->m_activeStateCount >= machine->m_activeStateCapacity) {
        size_t newCapacity = machine->m_activeStateCapacity * 2;
        XAbstractState** newStates = (XAbstractState**)XRealloc_System(
            machine->m_activeStates, sizeof(XAbstractState*) * newCapacity
        );
        if (!newStates) return;

        machine->m_activeStates = newStates;
        machine->m_activeStateCapacity = newCapacity;
    }

    machine->m_activeStates[machine->m_activeStateCount++] = state;
}

void XStateMachine_removeActiveState(XStateMachine* machine, XAbstractState* state) {
    if (!machine || !state) return;

    for (size_t i = 0; i < machine->m_activeStateCount; i++) {
        if (machine->m_activeStates[i] == state) {
            // 前移元素
            machine->m_activeStateCount--;
            for (size_t j = i; j < machine->m_activeStateCount; j++) {
                machine->m_activeStates[j] = machine->m_activeStates[j + 1];
            }
            break;
        }
    }
}

void VXStateMachine_deinit(XStateMachine* machine)
{
    if (!machine) return;

    // 停止状态机
    XStateMachine_stop(machine);

    // 释放激活状态列表
    XFree_System(machine->m_activeStates);
    //调用父类释放函数
    XVtableGetFunc(XObject_class_init(), EXClass_Deinit, void(*)(XObject*))(machine);
}

void XStateMachine_enterState(XStateMachine* machine, XAbstractState* state) {
    if (!machine || !state || XStateMachine_isActive(machine, state)) {
        return;
    }

    // 对于非顶层状态，先进入其父状态
    XState* parent = XAbstractState_parentState(state);
    if (parent && !XStateMachine_isActive(machine, (XAbstractState*)parent)) {
        XStateMachine_enterState(machine, (XAbstractState*)parent);
    }

    // 特殊处理不同类型的状态
    switch (state->m_type) {
    case XStateType_Basic:
        XState_activate_base((XState*)state);
        break;
    case XStateType_Final:
        XFinalState_activate((XFinalState*)state);
        break;
    case XStateType_Parallel:
        // 并行状态处理（类似于基本状态，但需要激活所有子状态）
        XState_activate_base((XState*)state);
        break;
    }

    // 添加到激活状态列表 (只在这里添加一次)
    XStateMachine_addActiveState(machine, state);
}

void XStateMachine_exitState(XStateMachine* machine, XAbstractState* state) {
    if (!machine || !state || !XStateMachine_isActive(machine, state)) {
        return;
    }

    // 退出状态
    XAbstractState_deactivate_base(state);

    // 对于历史状态，存储当前子状态
    if (state->m_parentState && ((XAbstractState*)state->m_parentState)->m_type == XStateType_History) {
        XHistoryState_storeState((XHistoryState*)state->m_parentState, state);
    }

    // 从激活状态列表移除
    XStateMachine_removeActiveState(machine, state);

    // 退出子状态（历史状态和最终状态没有子状态）
    if (state->m_type == XStateType_Basic || state->m_type == XStateType_Parallel) 
    {
        XState* basicState = (XState*)state;
        for (size_t i = 0; i < XState_childCount(basicState); i++) {
            XAbstractState* child = XState_child(basicState, i);
            XStateMachine_exitState(machine, child);
        }
    }
}
