#include "XAbstractState.h"
#include "XStateMachine.h"
#include "XState.h"
#include "XStack.h"
#include "XMemory.h"
#include <string.h>
#define INITIAL_CAPACITY 4
bool XStateMachine_isActive(const XStateMachine* machine, const XAbstractState* state);
void XStateMachine_addActiveState(XStateMachine* machine, XAbstractState* state);
void XStateMachine_removeActiveState(XStateMachine* machine, XAbstractState* state);

// 状态私有数据结构
typedef struct {
    XStateEnteredCallback enteredCallback;
    XStateExitedCallback exitedCallback;
} XAbstractStatePrivate;
static void VXAbstractState_onEntered(XAbstractState* state);
static void VXAbstractState_onExited(XAbstractState* state);
static void VXAbstractState_setMachine(XAbstractState* state, XStateMachine* machine);
static void VXAbstractState_setParentState(XAbstractState* state, XAbstractState* parent);
static void VXAbstractState_deinit(XAbstractState* state);
XVtable* XAbstractState_class_init()
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XAbstractState))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_DEFAULT(XClass_class_init());

    void* table[] = 
    {
        VXAbstractState_onEntered,VXAbstractState_onExited,
        VXAbstractState_setMachine,VXAbstractState_setParentState
    };

    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractState_deinit);
#if SHOWCONTAINERSIZE
    printf("XAbstractState size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

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

    XClass_init(state);
    XClassGetVtable(state) = XAbstractState_class_init();
    state->type = type;
    state->parentState = NULL;
    state->machine = NULL;
    state->isRunning = false;
    state->userData = NULL;
    state->privateData = NULL;
    //state->childCapacity = 0;
    //state->childStates = 0;
    //state->childCount = 0;

    // 初始化私有数据
    XAbstractState_private_init(state);
}
void VXAbstractState_onEntered(XAbstractState* state)
{
    if (!state || state->isRunning || !state->machine || !XStateMachine_isRunning(state->machine)) return;

    state->isRunning = true;

    // 触发进入回调
    XAbstractStatePrivate* d = XAbstractState_private(state);
    if (d->enteredCallback) {
        d->enteredCallback(state);
    }
    XStateMachine_addActiveState(state->machine, state);
    // 发送状态进入信号
    XStateMachine_entered_signal(state->machine, state);
}
void VXAbstractState_onExited(XAbstractState* state)
{
    if (!state || !state->isRunning || !state->machine || !XStateMachine_isRunning(state->machine)) return;
   
    // 触发退出回调
    XAbstractStatePrivate* d = XAbstractState_private(state);
    if (d->exitedCallback) {
        d->exitedCallback(state);
    }

    state->isRunning = false;
    XStateMachine_removeActiveState(state->machine, state);
    // 发送状态退出信号
    XStateMachine_exited_signal(state->machine, state);
}
void VXAbstractState_setMachine(XAbstractState* state, XStateMachine* machine)
{
    state->machine = machine;
}
void VXAbstractState_setParentState(XAbstractState* state, XAbstractState* parent)
{
    if (state->parentState == parent)
        return;
    state->parentState = parent;
    XAbstractState_setMachine_base(state, parent->machine);
}
void VXAbstractState_deinit(XAbstractState* state)
{
    if (!state) return;
   
    // 释放私有数据
    XMemory_free(state->privateData);
    state->privateData = NULL;
    memset(state,0,sizeof(state));
}
XStateType XAbstractState_type(const XAbstractState* state) {
    return state ? state->type : XStateType_Basic;
}

XState* XAbstractState_parentState(const XAbstractState* state) {
    return state ? state->parentState : NULL;
}

void XAbstractState_setParentState_base(XAbstractState* state, XAbstractState* parent)
{
    if (ISNULL(state, "") || ISNULL(XClassGetVtable(state), ""))
        return;
    XClassGetVirtualFunc(state, EXAbstractState_SetParentState, void(*)(XAbstractState*, XAbstractState*))(state, parent);
}

void XAbstractState_setMachine_base(XAbstractState* state, XStateMachine* machine)
{
    if (ISNULL(state, "") || ISNULL(XClassGetVtable(state), ""))
        return;
    XClassGetVirtualFunc(state, EXAbstractState_SetMachine, void(*)(XAbstractState*, XStateMachine*))(state, machine);
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

void XAbstractState_onEntered_base(XAbstractState* state) 
{
    if (ISNULL(state, "") || ISNULL(XClassGetVtable(state), ""))
        return;
    XClassGetVirtualFunc(state, EXAbstractState_OnEntered, void(*)(XAbstractState*))(state);  
}

void XAbstractState_onExited_base(XAbstractState* state)
{
    if (ISNULL(state, "") || ISNULL(XClassGetVtable(state), ""))
        return;
    XClassGetVirtualFunc(state, EXAbstractState_OnExited, void(*)(XAbstractState*))(state);
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