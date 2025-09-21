#include "XSignalTransition.h"
#include "XStateMachine.h"
#include "XMemory.h"
#include <string.h>
static void XSignalTransition_onSignalTriggered(XSignalTransition* transition, XStateMachine* machine);
// 信号槽回调函数
static void signalSlotCallback(XObject* sender, XObject* receiver, void* args) 
{
    XSignalTransition* transition = (XSignalTransition*)receiver;
    XStateMachine* machine = (XStateMachine*)args;

    if (transition && machine) {
        XSignalTransition_onSignalTriggered(transition, machine);
    }
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
    transition->m_sender = sender;
    transition->m_signal = signal;
    return transition;
}

void XSignalTransition_init(XSignalTransition* transition) 
{
    if (!transition) return;

    XAbstractTransition_init(&transition->m_class, XSignalTransitionType);
    transition->m_sender = NULL;
    transition->m_signal = NULL;
    transition->m_connection = NULL;

    transition->m_connection = NULL;
}

//void XSignalTransition_destroy(XSignalTransition* transition) {
//    if (!transition) return;
//
//    // 断开信号连接
//    if (transition->m_connection) {
//        XObject_disconnect_conn(transition->m_connection);
//        transition->m_connection = NULL;
//    }
//
//    // 释放信号名称
//    if (transition->m_signal) {
//        free((void*)transition->m_signal);
//        transition->m_signal =0;
//    }
//
//    //XAbstractTransition_destroy(&transition->m_class);
//}

XObject* XSignalTransition_sender(const XSignalTransition* transition) {
    return transition ? transition->m_sender : NULL;
}

const char* XSignalTransition_signal(const XSignalTransition* transition) {
    return transition ? transition->m_signal : NULL;
}

bool XSignalTransition_connect(XSignalTransition* transition, XObject* sender, size_t signal, XStateMachine* machine,XConnectionType type)
{
    if(!transition||!machine)
        return false;
    transition->m_sender = sender;
    transition->m_signal = signal;
    transition->m_connection=XObject_connect(sender?sender:machine,signal, machine, signalSlotCallback,type);
    return transition->m_connection != NULL;
}

void XSignalTransition_onSignalTriggered(XSignalTransition* transition, XStateMachine* machine) 
{
    if (!transition || !machine) return;

    // 执行转换
    XAbstractTransition_execute(&transition->m_class, machine, NULL);
}