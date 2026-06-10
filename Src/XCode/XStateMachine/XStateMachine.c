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

    //void* table[] = {
    //  
    //};

    //XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStateMachine_deinit);
    //XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, NULL);
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
        // 这里不需要实际存储状态，状态通过父状态关系管理
        //((XAbstractState*)state)->m_machine = m_machine;
        XAbstractState_setMachine_base(state,machine);
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

    // 进入初始状态
    XStateMachine_enterState(machine, machine->m_initialState);

  

    // 发送启动信号
    XStateMachine_start_signal(machine);

    return true;
}

void XStateMachine_stop(XStateMachine* machine) {
    if (!machine || machine->m_status == XStateMachineStopped) return;

    // 退出所有激活状态
    for (size_t i = 0; i < machine->m_activeStateCount; i++) {
        XStateMachine_exitState(machine, machine->m_activeStates[i]);
    }
    machine->m_activeStateCount = 0;

    // 设置状态机为停止状态
    machine->m_status = XStateMachineStopped;

    // 发送停止信号
    XStateMachine_stop_signal(machine);
}

void XStateMachine_pause(XStateMachine* machine) {
    if (machine && machine->m_status == XStateMachineRunning) {
        machine->m_status = XStateMachinePaused;
        XStateMachine_pause_signal(machine);
    }
}

void XStateMachine_resume(XStateMachine* machine) {
    if (machine && machine->m_status == XStateMachinePaused) {
        machine->m_status = XStateMachineRunning;
        XStateMachine_resume_signal(machine);
    }
}

XStateMachineStatus XStateMachine_status(const XStateMachine* machine) {
    return machine ? machine->m_status : XStateMachineStopped;
}
//void (*XEventCB)(XEvent* event)
//处理事件的回调
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
    //return eventHandled;
}

// 辅助函数：判断状态是否为另一个状态的后代
static bool isDescendant(XAbstractState* ancestor, XAbstractState* descendant) {
    if (!ancestor || !descendant) return false;
    if (ancestor == descendant) return true;

    XAbstractState* parent = XAbstractState_parentState(descendant);
    while (parent) {
        if (parent == ancestor) return true;
        parent = XAbstractState_parentState(parent);
    }
    return false;
}

/**
 * @brief 状态机转换逻辑
 * 修复点：深层历史恢复时避免父状态异常退出
 */
bool XStateMachine_transition(XStateMachine* machine, XAbstractState* source, XAbstractState* target) {
    if (!machine || !source || !target || machine->m_status != XStateMachineRunning) {
        return false;
    }

    XAbstractState* actualTarget = target;
    XVector* deepHistoryChain = NULL;
    bool isDeepHistory = false;

    // 解析历史状态
    if (target->m_type == XStateType_History) {
        XHistoryState* history = (XHistoryState*)target;
        actualTarget = XHistoryState_getActivatedTarget(history);;

        if (history->m_historyType == XHistoryStateType_Deep) {
            deepHistoryChain = XHistoryState_storedDeep(history);
            isDeepHistory = true;
        }

        if (!actualTarget) return false;
    }

    // 判断目标是否为源状态的后代
    bool targetIsDescendant = isDescendant(source, actualTarget);

    // 处理退出逻辑：深层历史不退出源状态（修复点）
    if (!targetIsDescendant && !isDeepHistory) {
        XStateMachine_exitState(machine, source);
    }
    else if (source->m_type == XStateType_Basic) {
        XState* basicSource = (XState*)source;
        for (size_t i = 0; i < XState_childCount(basicSource); i++) {
            XAbstractState* child = XState_child(basicSource, i);
            if (child->m_isRunning && !isDescendant(child, actualTarget)) {
                XStateMachine_exitState(machine, child);
            }
        }
    }

    // 激活深层历史链
    if (deepHistoryChain && XVector_size_base(deepHistoryChain) > 0) {
        // 按顺序激活链中所有状态（从顶层到深层）
        for (size_t i = 0; i < XVector_size_base(deepHistoryChain); i++) {
            XAbstractState** statePtr = XVector_at_base(deepHistoryChain, i);
            XAbstractState* stateInChain = *statePtr;

            if (!stateInChain || stateInChain->m_isRunning) continue;

            // 对于基本状态，跳过初始状态激活，避免覆盖历史
            if (stateInChain->m_type == XStateType_Basic) {
                ((XState*)stateInChain)->m_skipInitialState = true;
            }

            XStateMachine_enterState(machine, stateInChain);
        }
    }
    else {
        // 非深层历史：激活实际目标
        XStateMachine_enterState(machine, actualTarget);
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
    if (!machine || machine->m_activeStateCount == 0) return false;

    // 检查所有激活状态是否都是最终状态
    for (size_t i = 0; i < machine->m_activeStateCount; i++) {
        if (machine->m_activeStates[i]->m_type != XStateType_Final) {
            return false;
        }
    }
    return true;
}

void XStateMachine_setUserData(XStateMachine* machine, void* data) {
    if (machine) {
        machine->m_userData = data;
    }
}

void* XStateMachine_userData(const XStateMachine* machine) {
    return machine ? machine->m_userData : NULL;
}

void* XStateMachine_entered_signal(XStateMachine* machine, XAbstractState* state)
{
    if (machine)
        XObject_emitSignal(machine, XStateMachine_entered_signal, state,NULL,NULL, XEVENT_PRIORITY_NORMAL);
    return XStateMachine_entered_signal;
}

void* XStateMachine_exited_signal(XStateMachine* machine, XAbstractState* state)
{
    if (machine)
        XObject_emitSignal(machine, XStateMachine_exited_signal, state, NULL, NULL, XEVENT_PRIORITY_NORMAL);
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
        XAbstractState** newStates = (XAbstractState**)XCalloc_System(
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
 /*   case XStateType_History:
        XHistoryState_activate_base((XHistoryState*)state);*/
        break;
    case XStateType_Parallel:
        // 并行状态处理（类似于基本状态，但需要激活所有子状态）
        XState_activate_base((XState*)state);
        break;
    }

    // 添加到激活状态列表
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