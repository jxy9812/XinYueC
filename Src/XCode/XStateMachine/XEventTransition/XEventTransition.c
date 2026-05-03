#include "XEventTransition.h"
#include "XStateMachine.h"
#include "XMemory.h"
static void XEventTransition_deinit(XEventTransition* transition);
// 事件过滤回调函数
static bool eventFilterCallback(XObject* object, XEvent* event, void* userData) {
    XEventTransition* transition = (XEventTransition*)userData;
    XStateMachine* machine = (XStateMachine*)object;

    if (transition && machine && event) {
        return XEventTransition_processEvent(transition, machine, event);
    }
    return false;
}

XVtable* XEventTransition_class_init()
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XEventTransition))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_XCLASS(XAbstractTransition);

    /*  void* table[] =
      {
      };

      XVTABLE_ADD_FUNC_LIST_DEFAULT(table);*/
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XEventTransition_deinit);
#if SHOWCONTAINERSIZE
    printf("XEventTransition size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

XEventTransition* XEventTransition_create(XEventType eventType) {
    XEventTransition* transition = (XEventTransition*)XMemory_malloc(sizeof(XEventTransition));
    if (transition) {
        XEventTransition_init(transition, eventType);
        Set_Class_MemoryFree(transition, XFree);
    }
    return transition;
}

void XEventTransition_init(XEventTransition* transition, XEventType eventType) {
    if (!transition) return;

    XAbstractTransition_init(transition, XEventTransitionType);
    transition->m_eventType = eventType;
    XClassGetVtable(transition) = XEventTransition_class_init();
    // 注册事件过滤器
    // 注意：实际使用时需要在添加到状态机时完成事件过滤注册
}


XEventType XEventTransition_eventType(const XEventTransition* transition) {
    return transition ? transition->m_eventType : 0;
}

void XEventTransition_setEventType(XEventTransition* transition, XEventType eventType) {
    if (transition) {
        transition->m_eventType = eventType;
    }
}

bool XEventTransition_processEvent(XEventTransition* transition, XStateMachine* machine, const XEvent* event) {
    if (!transition || !machine || !event) return false;

    // 检查事件类型是否匹配
    if (event->type != transition->m_eventType) {
        return false;
    }

    //// 检查转换条件
    //if (!XAbstractTransition_checkCondition(&transition->m_class, event)) {
    //    return false;
    //}

    // 执行转换
    transition->m_event = event;
    bool is_ok=XAbstractTransition_execute(transition, machine);
    transition->m_event = NULL;
    return is_ok;
}

void XEventTransition_deinit(XEventTransition* transition)
{
    //调用父类释放函数
    XVtableGetFunc(XAbstractTransition_class_init(), EXClass_Deinit, void(*)(XAbstractState*))(transition);
}
