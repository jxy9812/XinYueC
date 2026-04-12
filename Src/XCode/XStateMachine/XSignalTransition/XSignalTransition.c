#include "XSignalTransition.h"
#include "XStateMachine.h"
#include "XMemory.h"
#include <string.h>
static void XSignalTransition_onSignalTriggered(XSignalTransition* transition, XStateMachine* machine);
// 信号槽回调函数
static void signalSlotCallback(XObject* sender, XObject* receiver, void* args) 
{
    XSignalTransition* transition = (XSignalTransition*)receiver;
    XStateMachine* machine = NULL;
    if (transition->m_class.m_sourceState)
        machine = transition->m_class.m_sourceState->m_machine;
    if (transition && machine)
    {
        transition->m_signalArgs = args;
        XSignalTransition_onSignalTriggered(transition, machine);
        transition->m_signalArgs = NULL;
    }
}
static void XSignalTransition_deinit(XSignalTransition* transition);
XVtable* XSignalTransition_class_init()
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XAbstractTransition))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_DEFAULT(XAbstractTransition_class_init());

    /*  void* table[] =
      {
      };

      XVTABLE_ADD_FUNC_LIST_DEFAULT(table);*/
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XSignalTransition_deinit);
#if SHOWCONTAINERSIZE
    printf("XSignalTransition size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}
XSignalTransition* XSignalTransition_create() 
{
    XSignalTransition* transition = (XSignalTransition*)XMemory_malloc(sizeof(XSignalTransition));
    if (transition) {
        XSignalTransition_init(transition);
    }
    return transition;
}

XSignalTransition* XSignalTransition_create_signal(XObject* sender, size_t signal)
{
    XSignalTransition* transition = XSignalTransition_create();
    XSignalTransition_connect(transition,sender,signal,XConnectionType_Queued);
    return transition;
}

void XSignalTransition_init(XSignalTransition* transition) 
{
    if (!transition) return;

    XAbstractTransition_init(transition, XSignalTransitionType);
    XClassGetVtable(transition) = XSignalTransition_class_init();
    transition->m_sender = NULL;
    transition->m_signal = NULL;
    transition->m_connection = NULL;

    transition->m_connection = NULL;
}

XObject* XSignalTransition_sender(const XSignalTransition* transition) {
    return transition ? transition->m_sender : NULL;
}

const char* XSignalTransition_signal(const XSignalTransition* transition) {
    return transition ? transition->m_signal : NULL;
}

bool XSignalTransition_connect(XSignalTransition* transition, XObject* sender, size_t signal,XConnectionType type)
{
    if(!transition)
        return false;
    transition->m_connection=XObject_connect1(sender?sender:transition,signal, transition, signalSlotCallback,type);
    if (transition->m_connection)
    {
        transition->m_sender = sender;
        transition->m_signal = signal;
    }
    return transition->m_connection != NULL;
}

void XSignalTransition_onSignalTriggered(XSignalTransition* transition, XStateMachine* machine) 
{
    if (!transition || !machine) return;

    // 执行转换
    XAbstractTransition_execute(transition, machine);
}

void XSignalTransition_deinit(XSignalTransition* transition)
{
     // 断开信号连接
    if (transition->m_connection) 
    {
        XObject_disconnect2(transition->m_connection);
        transition->m_connection = NULL;
    }
    transition->m_signal = 0;
    //调用父类释放函数
    XVtableGetFunc(XAbstractTransition_class_init(), EXClass_Deinit, void(*)(XAbstractTransition*))(transition);
}
