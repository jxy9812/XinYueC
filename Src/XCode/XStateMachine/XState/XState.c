#include "XState.h"
#include "XMemory.h"
#include "XStack.h"
#include <string.h>
#define INITIAL_CAPACITY 4
bool XStateMachine_isActive(const XStateMachine* machine, const XAbstractState* state);
void XStateMachine_addActiveState(XStateMachine* machine, XAbstractState* state);
void XStateMachine_removeActiveState(XStateMachine* machine, XAbstractState* state);

static void VXState_onEntered(XState* state);
static void VXState_onExited(XState* state);
static void VXState_setMachine(XState* state, XStateMachine* machine);
static void VXState_setParentState(XState* state, XAbstractState* parent);
static void VXState_deinit(XState* state);
XVtable* XState_class_init()
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XState))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_DEFAULT(XAbstractState_class_init());

   /* void* table[] =
    {
        VXAbstractState_onEntered,VXAbstractState_onExited
    };

    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);*/
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXState_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_OnEntered, VXState_onEntered);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_OnExited, VXState_onExited);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_SetMachine, VXState_setMachine);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_SetParentState, VXState_setParentState);
#if SHOWCONTAINERSIZE
    printf("XState size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

XState* XState_create() {
    XState* state = (XState*)XMemory_malloc(sizeof(XState));
    if (state) {
        XState_init(state);
    }
    return state;
}

void XState_init(XState* state) {
    if (!state) return;

    XAbstractState_init(state, XStateType_Basic);
    XClassGetVtable(state) = XState_class_init();
    state->transitionCapacity = INITIAL_CAPACITY;
    state->transitions = (XAbstractTransition**)XMemory_malloc(
        sizeof(XAbstractTransition*) * state->transitionCapacity
    );
    state->transitionCount = 0;

    state->initialState = NULL;
    state->childCapacity = 0;
    state->childStates = 0;
    state->childCount = 0;
}
bool XState_removeState(XState* state, XAbstractState* child)
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
            XAbstractState_setParentState_base(child, NULL);
            return true;
        }
    }

    return false;
}
size_t XState_childCount(const XState* state)
{
    return state ? state->childCount : 0;
}
XAbstractState* XState_child(const XState* state, size_t index)
{
    if (!state || index >= state->childCount) return NULL;
    return state->childStates[index];
}
bool XState_addState(XState* state, XAbstractState* child)
{
    if (!state || !child) return false;
    if (((XAbstractState*)state)->machine != ((XAbstractState*)child)->machine && ((XAbstractState*)child)->machine != NULL)return false;
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
    XAbstractState_setParentState_base(child, state);
    ((XAbstractState*)child)->machine = ((XAbstractState*)state)->machine;
    return true;
}
void VXState_deinit(XState* state)
{
    // 移除所有子状态
    for (size_t i = 0; i < state->childCount; i++) {
        XAbstractState_delete_base(state->childStates[i]);
    }
    XMemory_free(state->childStates);
    state->childStates = NULL;

    // 移除所有转换
    for (size_t i = 0; i < state->transitionCount; i++) {
        XAbstractTransition_destroy(state->transitions[i]);
    }
    XMemory_free(state->transitions);
    state->transitions = NULL;
    //调用父类释放函数
    XVtableGetFunc(XAbstractState_class_init(), EXClass_Deinit, void(*)(XAbstractState*))(state);
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
            XState_addState(state,initialState);
        state->initialState = initialState;
    }
}

XAbstractState* XState_initialState(const XState* state) {
    return state ? state->initialState : NULL;
}
void VXState_onEntered(XState* state)
{
    // 激活当前状态
    //XAbstractState_onEntered_base(state);
    XVtableGetFunc(XAbstractState_class_init(), EXAbstractState_OnEntered ,void(*)(XAbstractState*))(state);

    // 如果有初始子状态，激活它
    if (state->initialState) {
        // 递归激活子状态
        if (state->initialState->type == XStateType_Basic) {
            XState_activate_base((XState*)state->initialState);
        }
        /* else {
             XAbstractState_onEntered_base(state->initialState, machine);
         }*/
    }
}

void VXState_onExited(XState* state)
{
    if (!state||!state->parent.machine) return;

    // 失活所有子状态
    for (size_t i = 0; i < state->childCount; i++) {
        XAbstractState* child = state->childStates[i];
        if (child->isRunning) {
            /* if (child->type == XStateType_Basic) {
                 XAbstractState_onExited_base((XState*)child);
             }
             else */
            {
                XAbstractState_onExited_base(child);
            }
        }
    }

    XVtableGetFunc(XAbstractState_class_init(), EXAbstractState_OnExited, void(*)(XAbstractState*))(state);
}
void VXState_setMachine(XState* state, XStateMachine* machine)
{
    //设置子状态所属的状态机
    XStack* stack = XStack_create(sizeof(XAbstractState*));
    XStack_push_base(stack, &state);
    while (!XStack_isEmpty_base(stack))
    {
        XAbstractState* current = XStack_Top_Base(stack, XAbstractState*);
        XStack_pop_base(stack);
        if(XClassGetVtable(current) == XState_class_init())//看虚函数表判断是否是XState类
        {
            for (size_t i = 0; i < ((XState*)current)->childCount; i++)
            {
                XStack_push_base(stack, ((XState*)current)->childStates + i);
            }
        }
        ((XAbstractState*)current)->machine = machine;
    }
    XStack_delete_base(stack);
    XVtableGetFunc(XAbstractState_class_init(), EXAbstractState_SetMachine, void(*)(XAbstractState*, XStateMachine*))(state, machine);
}
void VXState_setParentState(XState* state, XAbstractState* parent)
{
    if (((XAbstractState*)state)->parentState == parent)
        return;
    if (((XAbstractState*)state)->parentState != NULL)
    {//先从原来的父状态删除
        XState_removeState(((XAbstractState*)state)->parentState, state);
    }
    ((XAbstractState*)state)->parentState = parent;
    XAbstractState_setMachine_base(state, parent->machine);
}
void XState_activate_base(XState* state) 
{
    if (!state) return;

    XAbstractState_onEntered_base(state);
    //XStateMachine_addActiveState(machine, state);
}

void XState_deactivate_base(XState* state)
{
    XAbstractState_onExited_base(state);
}