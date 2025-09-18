#include "XSignalTransition.h"
#include "XStateMachine.h"
#include "XMemory.h"
#include <string.h>

// 信号槽回调函数
static void signalSlotCallback(XObject* sender, XObject* receiver, void* args) {
    XSignalTransition* transition = (XSignalTransition*)receiver;
    XStateMachine* machine = (XStateMachine*)args;

    if (transition && machine) {
        XSignalTransition_onSignalTriggered(transition, machine);
    }
}

XSignalTransition* XSignalTransition_create(XObject* sender, const char* signal) {
    XSignalTransition* transition = (XSignalTransition*)XMemory_malloc(sizeof(XSignalTransition));
    if (transition) {
        XSignalTransition_init(transition, sender, signal);
    }
    return transition;
}

void XSignalTransition_init(XSignalTransition* transition, XObject* sender, const char* signal) {
    if (!transition) return;

    XAbstractTransition_init(&transition->parent, XSignalTransitionType);
    transition->sender = sender;
    transition->signal = signal ? strdup(signal) : NULL;
    transition->connection = NULL;

    // 连接信号（修正参数类型）
    if (sender && signal) {
        transition->connection = XObject_connect(
            sender,
            signal,  // 直接传递字符串信号
            (XObject*)&transition->parent,
            (XSlotFunc)signalSlotCallback,
            XConnectionType_Auto
        );
    }
}

void XSignalTransition_destroy(XSignalTransition* transition) {
    if (!transition) return;

    // 断开信号连接
    if (transition->connection) {
        XObject_disconnect_conn(transition->connection);
        transition->connection = NULL;
    }

    // 释放信号名称
    if (transition->signal) {
        free((void*)transition->signal);
        transition->signal = NULL;
    }

    XAbstractTransition_destroy(&transition->parent);
}

XObject* XSignalTransition_sender(const XSignalTransition* transition) {
    return transition ? transition->sender : NULL;
}

const char* XSignalTransition_signal(const XSignalTransition* transition) {
    return transition ? transition->signal : NULL;
}

void XSignalTransition_onSignalTriggered(XSignalTransition* transition, XStateMachine* machine) {
    if (!transition || !machine) return;

    // 执行转换
    XAbstractTransition_execute(&transition->parent, machine, NULL);
}