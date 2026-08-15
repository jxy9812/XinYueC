#include "XFinalState.h"

#include "XMemory.h"

XVtable* XFinalState_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFinalState)
    XVTABLE_INHERIT_XCLASS(XAbstractState);
    return XVTABLE_DEFAULT;
}

XFinalState* XFinalState_create_ex(XMemoryType memory, XState* parent)
{
    XFinalState* state = XMemory_malloc(sizeof(XFinalState), memory);
    if (!state)
        return NULL;
    XFinalState_init_ex(state, parent);
    Set_Class_Memory(state, memory); Set_Class_IsHeap(state, true);
    return state;
}

void XFinalState_init(XFinalState* state)
{
    XFinalState_init_ex(state, NULL);
}

void XFinalState_init_ex(XFinalState* state, XState* parent)
{
    if (!state)
        return;
    XAbstractState_init((XAbstractState*)state, XAbstractState_FinalState, parent);
    XClassSetVtable(state, XFinalState);
}
