#include"XEvent.h"
#include"XMemory.h"
#include<string.h>
#include"XTimer.h"
#include"XLockFreeQueue.h"
#include"XHashMap.h"
#include"XListSLinked.h"
#include"XObject.h"
#include"XVariant.h"
#include"XSemaphore.h"
#include"XCoreApplication.h"
static void VXEvent_default_setAccepted(XEvent* event, bool accepted);
static XEvent* VXEvent_default_clone(const XEvent* event);

XCLASS_DEFINE_BEGING(XKeyEventClass)
XCLASS_DEFINE_EXTEND_END(XKeyEventClass, XEvent);
XCLASS_DEFINE_BEGING(XMouseEventClass)
XCLASS_DEFINE_EXTEND_END(XMouseEventClass, XEvent);

static XEvent* VXKeyEvent_clone(const XKeyEvent* event);
static XEvent* VXMouseEvent_clone(const XMouseEvent* event);

static XVtable* XKeyEvent_class_init(void)
{
	XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XKeyEventClass))
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	XVTABLE_INHERIT_XCLASS(XEvent);
	XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXKeyEvent_clone);
	return XVTABLE_DEFAULT;
}

static XVtable* XMouseEvent_class_init(void)
{
	XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XMouseEventClass))
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	XVTABLE_INHERIT_XCLASS(XEvent);
	XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXMouseEvent_clone);
	return XVTABLE_DEFAULT;
}
XVtable* XEvent_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XEvent))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_XCLASS(XClass);
	void* table[] = {
		VXEvent_default_setAccepted,VXEvent_default_clone
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	//XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIODevice_deinit);
#if SHOWCONTAINERSIZE
	printf("XEvent size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
XEvent* XEvent_create(XEventType code)
{
	XEvent* event = XMalloc_MultiPool(sizeof(XEvent));
	if (!event)return NULL;
	XEvent_init(event, code);
	Set_Class_MemoryFree(event, XFree_MultiPool);
	return event;
}
void XEvent_init(XEvent* event,XEventType type)
{
	if (!event)return;
	memset(((XClass*)event) + 1, 0, sizeof(XEvent) - sizeof(XClass));
	XClass_init(event);
	XClassSetVtable(event, XEvent);
	event->type = type;
}

static void  XEventFunc_deinit(XEventFunc* ev)
{
	if (ev->argList)
	{
		XVarList_delete(ev->argList);
	}
}
XVtable* XEventFunc_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XEvent))
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_XCLASS(XEvent);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XEventFunc_deinit);
#if SHOWCONTAINERSIZE
	printf("XEventFunc size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
XEventFunc* XEventFunc_create(XCallableToRun func, XVarList* argList, void(*del_argList)(XVarList*))
{
	XEventFunc* event = XMalloc_MultiPool(sizeof(XEventFunc));
	if (!event)return NULL;
	XEventFunc_init(event, func, argList, del_argList);
	Set_Class_MemoryFree(event, XFree_MultiPool);
	return event;
}
void XEventFunc_init(XEventFunc* event, void(*func)(XVarList*), XVarList* argList, void(*del_argList)(XVarList*))
{
	if (!event)return;
	XEvent_init(event, XEVENT_TYPE_FUNC_RUN);
	XClassGetVtable(event) = XEventFunc_class_init();
	event->func = func;
	event->argList = argList;
	if (event->argList)
		event->argList->argsDel = del_argList;
}

void XEventFunc_handler(XEventFunc* event)
{
	if (event && event->func)
		event->func(event->argList);
	XEvent_accept(event);
}

static void VXEventMetaCall_deinit(XEventMetaCall* ev)
{
	if (ev->ref_count)
	{
		if (XAtomic_fetch_sub_int32(ev->ref_count, 1, XAtomic_MemoryOrder_Relaxed) == 1)
		{
			if (ev->argList)
			{
				XVarList_delete(ev->argList);
			}
			XAtomic_delete(ev->ref_count);
		}
	}
}
XVtable* XEventMetaCall_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XEvent))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_XCLASS(XEvent);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXEventMetaCall_deinit);
#if SHOWCONTAINERSIZE
	printf("XEventMetaCall size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
XEventMetaCall* XEventMetaCall_create(XObject* sender,XSlotFunc1 func, XVarList* argList,XAtomic_int32_t* ref_count,XSemaphore* sem)
{
	XEventMetaCall* event = XMalloc_MultiPool(sizeof(XEventMetaCall));
	//XPrintf("XEventMetaCall:%p 创建\n", event);
	if (!event)return NULL;
	XEvent_init(event, XEVENT_TYPE_META_CALL);
	XClassGetVtable(event) = XEventMetaCall_class_init();
	event->sender = sender;
	event->func = func;
	event->argList = argList;
	event->ref_count = ref_count;
	event->sem = sem;
	Set_Class_MemoryFree(event, XFree_MultiPool);
	
	return event;
}

void XEventMetaCall_handler(XEventMetaCall* event, XObject* receiver)
{
	if (!event)
		return; 
	/*if (event->sem)
	{
		XPrintf("XEventMetaCall:%p 出问题了 %p\n", event,event->sem);
	}*/
	if (receiver)
	{
		receiver->m_sender = event->sender;
		if (event->func)
			event->func(receiver, event->argList);
		receiver->m_sender = NULL;
	}
	if (event->sem)
		XSemaphore_release(event->sem, 1);
	XEvent_accept(event);
}

void XEvent_accept(XEvent* event)
{
	if (event) event->accepted = true;
}

XEvent* XEvent_clone_base(const XEvent * event)
{
	if (ISNULL(event, "") || ISNULL(XClassGetVtable(event), ""))
		return NULL;
	return XClassGetVirtualFunc(event, EXEvent_Clone, XEvent*(*)(XEvent*))(event);
}

void XEvent_ignore(XEvent* event)
{
	if (event) event->accepted = false;
}

bool XEvent_isAccepted(const XEvent * event)
{
	return event ? event->accepted : false;
}

bool XEvent_isInputEvent(const XEvent* event)
{
	return event ? event->input_event : false;
}

bool XEvent_isPointerEvent(const XEvent* event)
{
	return event ? event->pointer_event : false;
}

bool XEvent_isSinglePointEvent(const XEvent* event)
{
	return event ? event->single_point_event : false;
}

void XEvent_setAccepted_base(XEvent* event, bool accepted)
{
	if (ISNULL(event, "") || ISNULL(XClassGetVtable(event), ""))
		return ;
	return XClassGetVirtualFunc(event, EXEvent_SetAccepted, void(*)(XEvent*,bool))(event, accepted);
}

bool XEvent_spontaneous(const XEvent * event)
{
	return event ? event->spontaneous : false;
}

XEventType XEvent_type(const XEvent* event)
{
	return event ? event->type : XEVENT_TYPE_NONE;
}

XKeyEvent* XKeyEvent_create(XEventType type, int key, XKeyboardModifiers modifiers)
{
	XKeyEvent* event = XNew(XKeyEvent);
	if (!event)
		return NULL;
	XKeyEvent_init(event, type, key, modifiers);
	Set_Class_MemoryFree(event, XFree_System);
	return event;
}

void XKeyEvent_init(XKeyEvent* event, XEventType type, int key, XKeyboardModifiers modifiers)
{
	if (!event)
		return;
	XEvent_init((XEvent*)event, type);
	XClassGetVtable(event) = XKeyEvent_class_init();
	event->m_class.input_event = true;
	event->m_key = key;
	event->m_modifiers = modifiers;
}

int XKeyEvent_key(const XKeyEvent* event)
{
	return event ? event->m_key : 0;
}

XKeyboardModifiers XKeyEvent_modifiers(const XKeyEvent* event)
{
	return event ? event->m_modifiers : XKeyboardModifier_NoModifier;
}

XMouseEvent* XMouseEvent_create(XEventType type, XMouseButton button,
	XKeyboardModifiers modifiers, XPoint position)
{
	XMouseEvent* event = XNew(XMouseEvent);
	if (!event)
		return NULL;
	XMouseEvent_init(event, type, button, modifiers, position);
	Set_Class_MemoryFree(event, XFree_System);
	return event;
}

void XMouseEvent_init(XMouseEvent* event, XEventType type, XMouseButton button,
	XKeyboardModifiers modifiers, XPoint position)
{
	if (!event)
		return;
	XEvent_init((XEvent*)event, type);
	XClassGetVtable(event) = XMouseEvent_class_init();
	event->m_class.input_event = true;
	event->m_class.pointer_event = true;
	event->m_class.single_point_event = true;
	event->m_button = button;
	event->m_modifiers = modifiers;
	event->m_position = position;
}

XMouseButton XMouseEvent_button(const XMouseEvent* event)
{
	return event ? event->m_button : XMouseButton_NoButton;
}

XKeyboardModifiers XMouseEvent_modifiers(const XMouseEvent* event)
{
	return event ? event->m_modifiers : XKeyboardModifier_NoModifier;
}

XPoint XMouseEvent_position(const XMouseEvent* event)
{
	XPoint position = { 0, 0 };
	return event ? event->m_position : position;
}
static int g_nextUserEventType = XEVENT_TYPE_USER;
int XEvent_registerEventType(int hint)
{
	if (hint >= XEVENT_TYPE_USER && hint <= XEVENT_TYPE_MAX_USER) {
		// TODO: 检查是否已被占用（简化版直接返回）
		return hint;
	}
	if (g_nextUserEventType <= XEVENT_TYPE_MAX_USER) {
		return g_nextUserEventType++;
	}
	return -1; // 失败
}

XEventDeferredDelete* XEventDeferredDelete_create(bool isDelete)
{
	XEventDeferredDelete* event = XMalloc_MultiPool(sizeof(XEventDeferredDelete));
	//XPrintf("XEventDeferredDelete:%p 创建\n", event);
	if (!event)return NULL;
	XEvent_init(event, XEVENT_TYPE_DEFERRED_DELETE);
	event->isDelete = isDelete;
	Set_Class_MemoryFree(event, XFree_MultiPool);
	
	return event;
}

void XEventDeferredDelete_handler(XEventDeferredDelete* event, XObject* receiver)
{
	receiver->was_deleted = true;
	if (XAtomic_load_uint32(&receiver->m_posted_events, XAtomic_MemoryOrder_Acquire)<=1)
	{//正式释放
		XAtomic_fetch_sub_uint32(&receiver->m_posted_events, 1, XAtomic_MemoryOrder_Release);
		receiver->is_deleting_children = true;
		XObject_destroyed_signal(receiver);
		if(event->isDelete)
			XClass_delete_base(receiver);
		else
			XClass_deinit_base(receiver);
		XEvent_accept(event);
	}
	else
	{//重新投递：递减计数后重新投递，确保下次检查时计数正确
		XAtomic_fetch_sub_uint32(&receiver->m_posted_events, 1, XAtomic_MemoryOrder_Release);
		XCoreApplication_postEvent(receiver, event, XEVENT_PRIORITY_LOWEST);
	}
}

XEventTimer* XEventTimer_create(XTimerId id)
{
	XEventTimer* event = XMalloc_MultiPool(sizeof(XEventTimer));
	
	if (!event)return NULL;
	XEvent_init(event, XEVENT_TYPE_TIMER);
	event->timerId = id;
	Set_Class_MemoryFree(event, XFree_MultiPool);
	//XPrintf("XEventTimer:%p 创建 type:%d\n", event,event->m_base.type);
	return event;
}
XTimerId XEventTimer_timerId(const XEventTimer* event)
{
	return (event && event->m_base.type== XEVENT_TYPE_TIMER) ?
		((XEventTimer*)event)->timerId : 0;
}

XEventSockAct* XEventSockAct_create(XFd fd, XSocketActType actType)
{
	XEventSockAct* event = XMalloc_MultiPool(sizeof(XEventSockAct));
	if (!event)return NULL;
	XEvent_init(event, XEVENT_TYPE_SOCK_ACT);
	event->fd = fd;
	event->actType = actType;
	Set_Class_MemoryFree(event, XFree_MultiPool);
	return event;
}
XEventSockClose* XEventSockClose_create(XFd fd)
{
	XEventSockClose* event = XMalloc_MultiPool(sizeof(XEventSockClose));
	if (!event)return NULL;
	XEvent_init(event, XEVENT_TYPE_SOCK_CLOSE);
	event->fd = fd;
	Set_Class_MemoryFree(event, XFree_MultiPool);
	return event;
}
XChildEvent* XChildEvent_create(XEventType type, XObject* child)
{
	XChildEvent* event = XMalloc_MultiPool(sizeof(XChildEvent));
	if (!event)return NULL;
	XEvent_init(event, type);
	event->child = child;
	Set_Class_MemoryFree(event, XFree_MultiPool);
	return event;
}

bool XChildEvent_added(const XChildEvent* e)
{
	XEvent* event = (XEvent*)e;
	return event && event->type == XEVENT_TYPE_CHILD_ADDED;
}

XObject* XChildEvent_child(const XChildEvent* e)
{
	XEvent* event = (XEvent*)e;
	return (event && (event->type == XEVENT_TYPE_CHILD_ADDED ||
		event->type == XEVENT_TYPE_CHILD_POLISHED ||
		event->type == XEVENT_TYPE_CHILD_REMOVED)) ?
		((XChildEvent*)event)->child : NULL;
}

bool XChildEvent_polished(const XChildEvent* e)
{
	XEvent* event = (XEvent*)e;
	return event && event->type == XEVENT_TYPE_CHILD_POLISHED;
}

bool XChildEvent_removed(const XChildEvent* e)
{
	XEvent* event = (XEvent*)e;
	return event && event->type == XEVENT_TYPE_CHILD_REMOVED;
}

void XChildEvent_handler(XChildEvent* event, XObject* receiver)
{
	//if (XChildEvent_added(event))
	//{
	//	XVector* children = receiver->children;
	//	if (!children)
	//	{
	//		receiver->children = XVector_create(sizeof(XObject*));
	//		children = receiver->children;
	//	}
	//	if (-1 == XVector_indexOf(children, &event->child, 0))//确保新父节点没有自己
	//		XVector_push_back_1_base(children, &event->child);
	//}
	//else if (XChildEvent_removed(event))
	//{
	//	XVector* children= receiver->children;
	//	XVector_remove_base(children, XVector_indexOf(children, &event->child, 0), 1);
	//}
	XEvent_accept(event);
}

XDynamicPropertyChangeEvent* XDynamicPropertyChangeEvent_create(const char* name)
{
	return NULL;
}

const char* XDynamicPropertyChangeEvent_propertyName(const XEvent* event)
{
	if (event && event->type == XEVENT_TYPE_DYNAMIC_PROPERTY_CHANGE) {
		return (const char*)((XDynamicPropertyChangeEvent*)event)->propertyName;
	}
	return NULL;
}

void VXEvent_default_setAccepted(XEvent* event, bool accepted)
{
	if (event) event->accepted = accepted;
}

XEvent* VXEvent_default_clone(const XEvent* event)
{
	XEvent* copy = (XEvent*)XMalloc_System(sizeof(XEvent));
	if (copy) {
		memcpy(copy, event, sizeof(XEvent));
		Set_Class_MemoryFree(copy, XFree_System);
	}
	return copy;
}

static XEvent* VXKeyEvent_clone(const XKeyEvent* event)
{
	XKeyEvent* copy = XNew(XKeyEvent);
	if (copy) {
		memcpy(copy, event, sizeof(XKeyEvent));
		Set_Class_MemoryFree(copy, XFree_System);
	}
	return (XEvent*)copy;
}

static XEvent* VXMouseEvent_clone(const XMouseEvent* event)
{
	XMouseEvent* copy = XNew(XMouseEvent);
	if (copy) {
		memcpy(copy, event, sizeof(XMouseEvent));
		Set_Class_MemoryFree(copy, XFree_System);
	}
	return (XEvent*)copy;
}
