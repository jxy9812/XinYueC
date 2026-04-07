#include"XEvent.h"
#include"XMemory.h"
#include<string.h>
#include"XTimerBase.h"
#include"XCircularQueueAtomic.h"
#include"XHashMap.h"
#include"XListSLinked.h"
#include"XObject.h"
#include"XVariant.h"
#include"XSemaphore.h"
#include"XCoreApplication.h"
static void VXEvent_default_setAccepted(XEvent* event, bool accepted);
static XEvent* VXEvent_default_clone(const XEvent* event);
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
	XVTABLE_INHERIT_DEFAULT(XClass_class_init());
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
	XEvent* event = XMemory_malloc(sizeof(XEvent));
	XEvent_init(event, code);
	return event;
}
void XEvent_init(XEvent* event,XEventType type)
{
	if (!event)return;
	memset(((XClass*)event) + 1, 0, sizeof(XEvent) - sizeof(XClass));
	XClass_init(event);
	XClassGetVtable(event) = XEvent_class_init();
	event->type = type;
}

static void  XEventFunc_deinit(XEventFunc* ev)
{
	if (ev->args && ev->del)
	{
		ev->del(ev->args);
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
		XVTABLE_INHERIT_DEFAULT(XEvent_class_init());
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XEventFunc_deinit);
#if SHOWCONTAINERSIZE
	printf("XEventFunc size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
XEventFunc* XEventFunc_create( void (*func)(void*), void* args, void(*del)(void*))
{
	XEventFunc* event = XMemory_malloc(sizeof(XEventFunc));
	if (!event)return NULL;
	XEventFunc_init(event, func,args,del);
	return event;
}

void XEventFunc_init(XEventFunc* event,void(*func)(void*), void* args, void(*del)(void*))
{
	if (!event)return;
	XEvent_init(event, XEVENT_TYPE_FUNC_RUN);
	XClassGetVtable(event) = XEventFunc_class_init();
	event->func = func;
	event->args = args;
	event->del = del;
}

void XEventFunc_handler(XEventFunc* event)
{
	if (event && event->func)
		event->func(event->args);
	XEvent_accept(event);
}

static void VXEventMetaCall_deinit(XEventMetaCall* ev)
{
	if (ev->ref_count)
	{
		if (XAtomic_fetch_sub_int32(ev->ref_count, 1) == 1)
		{
			if (ev->argList)
			{
				//ev->del(ev->argList);
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
	XVTABLE_INHERIT_DEFAULT(XEvent_class_init());
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXEventMetaCall_deinit);
#if SHOWCONTAINERSIZE
	printf("XEventMetaCall size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
XEventMetaCall* XEventMetaCall_create(XObject* sender,XSlotFunc func, XVarList* argList,XAtomic_int32_t* ref_count,XSemaphore* sem)
{
	XEventMetaCall* event = XMemory_malloc(sizeof(XEventMetaCall));
	if (event == NULL)
		return NULL;
	XEvent_init(event, XEVENT_TYPE_META_CALL);
	XClassGetVtable(event) = XEventMetaCall_class_init();
	event->sender = sender;
	event->func = func;
	event->argList = argList;
	//event->del = XVarList_delete;
	event->ref_count = ref_count;
	event->sem = sem;
	return event;
}

void XEventMetaCall_handler(XEventMetaCall* event, XObject* receiver)
{
	if (!event)
		return;
	if (event->func)
		event->func(receiver, event->argList, event->sender);

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
	XEventDeferredDelete* event = XNew(XEventDeferredDelete);
	XEvent_init(event, XEVENT_TYPE_DEFERRED_DELETE);
	event->isDelete = isDelete;
	return event;
}

void XEventDeferredDelete_handler(XEventDeferredDelete* event, XObject* receiver)
{
	receiver->was_deleted = true;
	if (XAtomic_fetch_sub_uint32(&receiver->m_eventCount, 1) == 1)
	{//正式释放
		receiver->is_deleting_children = true;
		XObject_deinit_signal(receiver);
		if(event->isDelete)
			XClass_delete_base(receiver);
		else
			XClass_deinit_base(receiver);
		XEvent_accept(event);
	}
	else
	{//重新投递
		XAtomic_fetch_add_int32(&receiver->m_eventCount, 1);
		XCoreApplication_postEvent(receiver, event, XEVENT_PRIORITY_LOWEST);
	}
}

XTimerEvent* XEventTimer_create_id(XTimerId id)
{
	XTimerEvent* event = XNew(XTimerEvent);
	XEvent_init(event, XEVENT_TYPE_TIMER);
	event->timerId = id;
	return event;
}

XTimerEvent* XEventTimer_create_timerId(int timerId)
{
	return NULL;
}

XTimerId XEventTimer_id(const XTimerEvent* event)
{
	return (event && event->m_base.type== XEVENT_TYPE_TIMER) ?
		((XTimerEvent*)event)->timerId : 0;
}

int XEventTimer_timerId(const XTimerEvent* event)
{//待后续实现
	return 0;
}

XChildEvent* XChildEvent_create(XEventType type, XObject* child)
{
	return NULL;
}

bool XChildEvent_added(const XEvent* event)
{
	return event && event->type == XEVENT_TYPE_CHILD_ADDED;
}

XObject* XChildEvent_child(const XEvent* event)
{
	return (event && (event->type == XEVENT_TYPE_CHILD_ADDED ||
		event->type == XEVENT_TYPE_CHILD_POLISHED ||
		event->type == XEVENT_TYPE_CHILD_REMOVED)) ?
		((XChildEvent*)event)->child : NULL;
}

bool XChildEvent_polished(const XEvent* event)
{
	return event && event->type == XEVENT_TYPE_CHILD_POLISHED;
}

bool XChildEvent_removed(const XEvent* event)
{
	return event && event->type == XEVENT_TYPE_CHILD_REMOVED;
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
	XEvent* copy = (XEvent*)XMemory_malloc(sizeof(XEvent));
	if (copy) {
		memcpy(copy, event, sizeof(XEvent));
	}
	return copy;
}
