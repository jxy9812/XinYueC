#include "XEventTransition.h"
#include "XStateMachine.h"
#include "XMemory.h"

// 事件过滤回调函数
static bool eventFilterCallback(XObject* object, XEvent* event, void* userData) {
    XEventTransition* transition = (XEventTransition*)userData;
    XStateMachine* machine = (XStateMachine*)object;

    if (transition && machine && event) {
        return XEventTransition_processEvent(transition, machine, event);
    }
    return false;
}

XEventTransition* XEventTransition_create(XEventType eventType) {
    XEventTransition* transition = (XEventTransition*)XMemory_malloc(sizeof(XEventTransition));
    if (transition) {
        XEventTransition_init(transition, eventType);
    }
    return transition;
}

void XEventTransition_init(XEventTransition* transition, XEventType eventType) {
    if (!transition) return;

    XAbstractTransition_init(&transition->parent);
    transition->eventType = eventType;

    // 注册事件过滤器
    // 注意：实际使用时需要在添加到状态机时完成事件过滤注册
}

void XEventTransition_destroy(XEventTransition* transition) {
    if (!transition) return;

    // 移除事件过滤器
    // 注意：实际使用时需要在从状态机移除时完成

    XAbstractTransition_destroy(&transition->parent);
}

XEventType XEventTransition_eventType(const XEventTransition* transition) {
    return transition ? transition->eventType : 0;
}

void XEventTransition_setEventType(XEventTransition* transition, XEventType eventType) {
    if (transition) {
        transition->eventType = eventType;
    }
}

bool XEventTransition_processEvent(XEventTransition* transition, XStateMachine* machine, const XEvent* event) {
    if (!transition || !machine || !event) return false;

    // 检查事件类型是否匹配
    if (event->type != transition->eventType) {
        return false;
    }

    // 检查转换条件
    if (!XAbstractTransition_checkCondition(&transition->parent, event)) {
        return false;
    }

    // 执行转换
    return XAbstractTransition_execute(&transition->parent, machine, event);
}