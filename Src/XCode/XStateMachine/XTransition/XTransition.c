#include "XTransition.h"
#include "XState.h"
#include "XStateMachine.h"
#include "XMemory.h"
#include <assert.h>

/**
 * @brief 转换信号触发回调
 */
static void XSignalTransition_triggered(XObject* sender, XObject* receiver, void* args) {
    XSignalTransition* trans = (XSignalTransition*)receiver;
    if (trans && trans->parent.source && ((XAbstractState*)trans->parent.source)->machine) {
        // 创建一个虚拟事件触发转换
        XEventMin event = {
            .code = XEVENT_SIGNAL_TRIGGERED,
            .receiver = receiver,
            .userData= args
        };
        if (XTransition_check((XTransition*)trans, ((XAbstractState*)trans->parent.source)->machine, &event)) {
            XTransition_trigger((XTransition*)trans, ((XAbstractState*)trans->parent.source)->machine, &event);
        }
    }
}

/**
 * @brief 转换虚函数表初始化
 */
static XVtable* XTransition_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XTransition))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承XObject
        XVTABLE_INHERIT_DEFAULT(XObject_class_init());
    return XVTABLE_DEFAULT;
}

XTransition* XTransition_create(XState* source, XState* target) {
    if (!source || !target) return NULL;

    XTransition* transition = (XTransition*)XMemory_malloc(sizeof(XTransition));
    if (!transition) return NULL;

    XTransition_init(transition, source, target);
    return transition;
}

void XTransition_init(XTransition* transition, XState* source, XState* target) {
    if (!transition || !source || !target) return;

    XObject_init((XObject*)transition);
    XClassGetVtable(transition) = XTransition_class_init();

    transition->source = source;
    transition->target = target;
    transition->condition = NULL;
    transition->action = NULL;
    transition->is_enabled = true;

    // 将转换添加到源状态
    XState_addTransition(source, transition);
}

void XTransition_destroy(XTransition* transition) {
    if (!transition) return;

    // 从源状态移除转换
    if (transition->source) {
        XState_removeTransition(transition->source, transition);
    }

    // 调用基类销毁函数
    XObject_deinit_base((XObject*)transition);
    XMemory_free(transition);
}

//XEventTransition* XEventTransition_create(XState* source, XState* target, XEventType event_type) {
//    if (!source || !target) return NULL;
//
//    XEventTransition* transition = (XEventTransition*)XMemory_malloc(sizeof(XEventTransition));
//    if (!transition) return NULL;
//
//    XTransition_init((XTransition*)transition, source, target);
//    transition->event_type = event_type;
//
//    return transition;
//}

//XSignalTransition* XSignalTransition_create(XState* source, XState* target,
//    XObject* sender, const char* signal) {  // 修正信号类型为const char*
//    if (!source || !target || !sender || !signal) return NULL;
//
//    XSignalTransition* transition = (XSignalTransition*)XMemory_malloc(sizeof(XSignalTransition));
//    if (!transition) return NULL;
//
//    XTransition_init((XTransition*)transition, source, target);
//    transition->sender = sender;
//    transition->signal = strdup(signal);  // 存储信号字符串
//
//    // 连接到信号（修正参数类型）
//    transition->connection = XObject_connect(sender, signal,
//        (XObject*)transition,
//        XSignalTransition_triggered,
//        XConnectionType_Direct);
//
//    return transition;
//}

void XTransition_setCondition(XTransition* transition, XTransitionCondition condition) 
{
    if (transition) {
        transition->condition = condition;
    }
}

void XTransition_setAction(XTransition* transition, XTransitionAction action) {
    if (transition) {
        transition->action = action;
    }
}

void XTransition_setEnabled(XTransition* transition, bool enabled) {
    if (transition) {
        transition->is_enabled = enabled;
    }
}

bool XTransition_isEnabled(const XTransition* transition) {
    return transition ? transition->is_enabled : false;
}

XState* XTransition_source(const XTransition* transition) {
    return transition ? transition->source : NULL;
}

XState* XTransition_target(const XTransition* transition) {
    return transition ? transition->target : NULL;
}

bool XTransition_check(const XTransition* transition, XStateMachine* machine, const XEvent* event) {
    if (!transition || !machine || !event || !transition->is_enabled) {
        return false;
    }

    // 对于事件转换，检查事件类型
    //if (transition->m_class.type == XEVENT_TRANSITION)
    {
        XEventTransition* evt_trans = (XEventTransition*)transition;
        if (evt_trans->event_type != event->event.code) {
            return false;
        }
    }

    // 检查条件（如果有）
    if (transition->condition) {
        return transition->condition(transition,machine, event);
    }

    return true;
}

void XTransition_trigger(XTransition* transition, XStateMachine* machine, const XEvent* event) {
    if (!transition || !machine) return;

    // 执行转换动作（如果有）
    if (transition->action) {
        transition->action(transition, machine, event);
    }

    // 退出源状态（使用状态自身的失活方法）
    XState_deactivate(transition->source, machine);

    // 进入目标状态（使用状态自身的激活方法）
    XState_activate(transition->target, machine);

    // 发送转换触发信号
    //XObject_postEvent((XObject*)transition,
       // XEventSignal_create(XTransition_Signal_triggered, NULL, NULL),
        //XEVENT_PRIORITY_NORMAL);
}

XConnection* XTransition_connect(XTransition* transition, XTransitionSignal signal,
    XObject* receiver, XSlotFunc slot, XConnectionType type) {
    if (!transition || !receiver || !slot) return NULL;
    return XObject_connect((XObject*)transition, (size_t)signal, receiver, slot, type);
}