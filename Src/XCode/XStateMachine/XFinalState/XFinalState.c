#include "XFinalState.h"
#include "XStateMachine.h"
#include "XMemory.h"
XVtable* XFinalState_class_init()
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XFinalState))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_DEFAULT(XAbstractState_class_init());

    /* void* table[] =
     {
         VXAbstractState_activate,VXAbstractState_deactivate
     };

     XVTABLE_ADD_FUNC_LIST_DEFAULT(table);*/
   /* XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXState_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_Activate, VXState_activate);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_Deactivate, VXState_deactivate);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_SetMachine, VXState_setMachine);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_SetParentState, VXState_setParentState);*/
#if SHOWCONTAINERSIZE
    printf("XFinalState size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}
XFinalState* XFinalState_create() {
    XFinalState* state = (XFinalState*)XMemory_malloc(sizeof(XFinalState));
    if (state) {
        XFinalState_init(state);
    }
    return state;
}

void XFinalState_init(XFinalState* state) {
    if (!state) return;
    XAbstractState_init(&state->m_class, XStateType_Final);
}

void XFinalState_activate(XFinalState* state) {
    if (!state ) return;

    // 激活最终状态
    XAbstractState_activate_base(&state->m_class);

    // 发送状态机完成信号
    //XObject_emitSignal((XObject*)m_machine, "finished()", NULL);

    // 检查是否是顶层最终状态，如果是则停止状态机
    XState* parent = XAbstractState_parentState(&state->m_class);
    if (!parent || !XAbstractState_parentState((XAbstractState*)parent)) 
    {
        XStateMachine_stop(((XAbstractState*)state)->m_machine);
    }
}