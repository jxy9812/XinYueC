#include "XState.h"
#include "XMemory.h"
#include "XStack.h"
#include "XSignalTransition.h"
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
    state->m_transitionCapacity = INITIAL_CAPACITY;
    state->m_transitions = (XAbstractTransition**)XMemory_malloc(
        sizeof(XAbstractTransition*) * state->m_transitionCapacity
    );
    state->m_transitionCount = 0;

    state->m_initialState = NULL;
    state->m_childCapacity = 0;
    state->m_childStates = 0;
    state->m_childCount = 0;
}
bool XState_removeState(XState* state, XAbstractState* child)
{
    if (!state || !child) return false;

    for (size_t i = 0; i < state->m_childCount; i++) {
        if (state->m_childStates[i] == child) {
            // 前移元素
            state->m_childCount--;
            for (size_t j = i; j < state->m_childCount; j++) {
                state->m_childStates[j] = state->m_childStates[j + 1];
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
    return state ? state->m_childCount : 0;
}
XAbstractState* XState_child(const XState* state, size_t index)
{
    if (!state || index >= state->m_childCount) return NULL;
    return state->m_childStates[index];
}
bool XState_addState(XState* state, XAbstractState* child)
{
    if (!state || !child) return false;
    if (((XAbstractState*)state)->m_machine != ((XAbstractState*)child)->m_machine && ((XAbstractState*)child)->m_machine != NULL)return false;
    if (state->m_childCapacity == 0)
    {//初始化
        state->m_childCapacity = INITIAL_CAPACITY;
        state->m_childStates = (XAbstractState**)XMemory_malloc(
            sizeof(XAbstractState*) * state->m_childCapacity);
    }
    else
    {
        // 检查是否已存在
        for (size_t i = 0; i < state->m_childCount; i++) {
            if (state->m_childStates[i] == child) {
                return false;
            }
        }
    }


    // 扩容
    if (state->m_childCount >= state->m_childCapacity) {
        size_t newCapacity = state->m_childCapacity * 2;
        XAbstractState** newChildren = (XAbstractState**)XMemory_realloc(
            state->m_childStates, sizeof(XAbstractState*) * newCapacity
        );
        if (!newChildren) return false;

        state->m_childStates = newChildren;
        state->m_childCapacity = newCapacity;
    }

    // 添加子状态并设置父状态
    state->m_childStates[state->m_childCount++] = child;
    XAbstractState_setParentState_base(child, state);
    ((XAbstractState*)child)->m_machine = ((XAbstractState*)state)->m_machine;
    return true;
}
void VXState_deinit(XState* state)
{
    // 移除所有子状态
    for (size_t i = 0; i < state->m_childCount; i++) {
        XAbstractState_delete_base(state->m_childStates[i]);
    }
    XMemory_free(state->m_childStates);
    state->m_childStates = NULL;

    // 移除所有转换
    for (size_t i = 0; i < state->m_transitionCount; i++) {
        XAbstractTransition_delete_base(state->m_transitions[i]);
    }
    XMemory_free(state->m_transitions);
    state->m_transitions = NULL;
    //调用父类释放函数
    XVtableGetFunc(XAbstractState_class_init(), EXClass_Deinit, void(*)(XAbstractState*))(state);
}

bool XState_addTransition(XState* state, XAbstractTransition* transition) {
    if (!state || !transition) return false;

    // 检查是否已存在
    for (size_t i = 0; i < state->m_transitionCount; i++) {
        if (state->m_transitions[i] == transition) {
            return false;
        }
    }

    // 扩容
    if (state->m_transitionCount >= state->m_transitionCapacity) {
        size_t newCapacity = state->m_transitionCapacity * 2;
        XAbstractTransition** newTransitions = (XAbstractTransition**)XMemory_realloc(
            state->m_transitions, sizeof(XAbstractTransition*) * newCapacity
        );
        if (!newTransitions) return false;

        state->m_transitions = newTransitions;
        state->m_transitionCapacity = newCapacity;
    }

    // 添加转换
    state->m_transitions[state->m_transitionCount++] = transition;
    XAbstractTransition_setSourceState(transition, (XAbstractState*)state);

    return true;
}

bool XState_removeTransition(XState* state, XAbstractTransition* transition) {
    if (!state || !transition) return false;

    for (size_t i = 0; i < state->m_transitionCount; i++) {
        if (state->m_transitions[i] == transition) {
            // 前移元素
            state->m_transitionCount--;
            for (size_t j = i; j < state->m_transitionCount; j++) {
                state->m_transitions[j] = state->m_transitions[j + 1];
            }

            // 清除转换的源状态
            XAbstractTransition_setSourceState(transition, NULL);
            return true;
        }
    }

    return false;
}

size_t XState_transitionCount(const XState* state) {
    return state ? state->m_transitionCount : 0;
}

XAbstractTransition* XState_transition(const XState* state, size_t index) {
    if (!state || index >= state->m_transitionCount) return NULL;
    return state->m_transitions[index];
}

void XState_setInitialState(XState* state, XAbstractState* initialState) {
    if (state) 
    {
        if (initialState)
            XState_addState(state,initialState);
        state->m_initialState = initialState;
    }
}

XAbstractState* XState_initialState(const XState* state) {
    return state ? state->m_initialState : NULL;
}
void VXState_onEntered(XState* state)
{
    // 激活当前状态
    //XAbstractState_onEntered_base(state);
    XVtableGetFunc(XAbstractState_class_init(), EXAbstractState_OnEntered ,void(*)(XAbstractState*))(state);

    // 如果有初始子状态，激活它
    if (state->m_initialState) {
        // 递归激活子状态
        if (state->m_initialState->m_type == XStateType_Basic) {
            XState_activate_base((XState*)state->m_initialState);
        }
        /* else {
             XAbstractState_onEntered_base(state->m_initialState, m_machine);
         }*/
    }
}

void VXState_onExited(XState* state)
{
    if (!state||!state->m_class.m_machine) return;

    // 失活所有子状态
    for (size_t i = 0; i < state->m_childCount; i++) {
        XAbstractState* child = state->m_childStates[i];
        if (child->m_isRunning) {
            /* if (child->m_type == XStateType_Basic) {
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
            XState* state=((XState*)current);
            for (size_t i = 0; i < state->m_childCount; i++)
            {
                XStack_push_base(stack, state->m_childStates + i);
            }
            ////如果是信号转化条件，检查链接信号与槽
            //for (size_t i = 0; i < state->m_transitionCount; i++)
            //{
            //    if (XClassGetVtable(state->m_transitions[i]) == XSignalTransition_class_init())
            //    {//当前是信号转换类
            //        XSignalTransition* transition = state->m_transitions[i];
            //        if (transition->m_connection|| !transition->m_sender)
            //            continue;
            //        transition->m_connection=XSignalTransition_connect(transition,transition->m_sender,transition->m_signal,machine,XConnectionType_Auto);
            //    }
            //}
        }
        ((XAbstractState*)current)->m_machine = machine;
    }
    XStack_delete_base(stack);
    XVtableGetFunc(XAbstractState_class_init(), EXAbstractState_SetMachine, void(*)(XAbstractState*, XStateMachine*))(state, machine);
}
void VXState_setParentState(XState* state, XAbstractState* parent)
{
    if (((XAbstractState*)state)->m_parentState == parent)
        return;
    if (((XAbstractState*)state)->m_parentState != NULL)
    {//先从原来的父状态删除
        XState_removeState(((XAbstractState*)state)->m_parentState, state);
    }
    ((XAbstractState*)state)->m_parentState = parent;
    XAbstractState_setMachine_base(state, parent->m_machine);
}
void XState_activate_base(XState* state) 
{
    if (!state) return;

    XAbstractState_onEntered_base(state);
    //XStateMachine_addActiveState(m_machine, state);
}

void XState_deactivate_base(XState* state)
{
    XAbstractState_onExited_base(state);
}