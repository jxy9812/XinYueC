#include "XFinalState.h"

#include "XMemory.h"

XVtable* XFinalState_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFinalState)
    XVTABLE_INHERIT_XCLASS(XAbstractState);
    return XVTABLE_DEFAULT;
}

XFinalState* XFinalState_create(void)
{
    return XFinalState_create_ex(NULL);
}

XFinalState* XFinalState_create_ex(XState* parent)
{
    XFinalState* state = XNew(XFinalState);
    if (!state)
        return NULL;
    XFinalState_init_ex(state, parent);
    Set_Class_MemoryFree(state, XFree_System);
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
