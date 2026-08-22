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
#include"XThreadData.h"
#include<stdlib.h>
static void VXEvent_default_setAccepted(XEvent* event, bool accepted);
static void VXEvent_copy(XEvent* dest, const XEvent* src);
static XEvent* VXEvent_default_clone(const XEvent* event);

static void VXKeyEvent_copy(XKeyEvent* dest, const XKeyEvent* src);
static XEvent* VXKeyEvent_clone(const XKeyEvent* event);
static void VXMouseEvent_copy(XMouseEvent* dest, const XMouseEvent* src);
static XEvent* VXMouseEvent_clone(const XMouseEvent* event);

static XVtable* XKeyEvent_class_init(void)
{
	XVTABLE_INIT_DEFAULT(XKeyEvent)
	XVTABLE_INHERIT_XCLASS(XEvent);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXKeyEvent_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXKeyEvent_clone);
	return XVTABLE_DEFAULT;
}

static XVtable* XMouseEvent_class_init(void)
{
	XVTABLE_INIT_DEFAULT(XMouseEvent)
	XVTABLE_INHERIT_XCLASS(XEvent);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXMouseEvent_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXMouseEvent_clone);
	return XVTABLE_DEFAULT;
}
XVtable* XEvent_class_init()
{
	XVTABLE_INIT_DEFAULT(XEvent)
	//继承类
	XVTABLE_INHERIT_XCLASS(XClass);
	void* table[] = {
		VXEvent_default_setAccepted,VXEvent_default_clone
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXEvent_copy);
	//XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIODevice_deinit);
	XCLASS_SHOW_SIZE_DEFAULT(XEvent);
	return XVTABLE_DEFAULT;
}
XEvent* XEvent_create_ex(XMemoryType memory, XEventType code)
{
	XEvent* event = XMemory_malloc(sizeof(XEvent), memory);
	if (!event)return NULL;
	XEvent_init(event, code);
	Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
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
	XVTABLE_INIT_DEFAULT(XEventFunc)
	//继承类
	XVTABLE_INHERIT_XCLASS(XEvent);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XEventFunc_deinit);
	XCLASS_SHOW_SIZE_DEFAULT(XEventFunc);
	return XVTABLE_DEFAULT;
}
XEventFunc* XEventFunc_create_ex(XMemoryType memory, XCallableToRun func, XVarList* argList, void(*del_argList)(XVarList*))
{
	XEventFunc* event = XMemory_malloc(sizeof(XEventFunc), memory);
	if (!event)return NULL;
	XEventFunc_init(event, func, argList, del_argList);
	Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
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
	XEvent_accept((XEvent*)event);
}

static void VXMetaCallEvent_deinit(XMetaCallEvent* ev)
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
XVtable* XMetaCallEvent_class_init()
{
	XVTABLE_INIT_DEFAULT(XMetaCallEvent)
	//继承类
	XVTABLE_INHERIT_XCLASS(XEvent);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMetaCallEvent_deinit);
	XCLASS_SHOW_SIZE_DEFAULT(XMetaCallEvent);
	return XVTABLE_DEFAULT;
}
XMetaCallEvent* XMetaCallEvent_create_ex(XMemoryType memory, XObject* sender,XSlotFunc1 func, size_t signal_id, XVarList* argList,XAtomic_int32_t* ref_count,XSemaphore* sem)
{
	XMetaCallEvent* event = XMemory_malloc(sizeof(XMetaCallEvent), memory);
	//XPrintf("XMetaCallEvent:%p 创建\n", event);
	if (!event)return NULL;
	XEvent_init(event, XEVENT_TYPE_META_CALL);
	XClassGetVtable(event) = XMetaCallEvent_class_init();
	event->sender = sender;
	event->signal_id = signal_id;
	event->func = func;
	event->argList = argList;
	event->ref_count = ref_count;
	event->sem = sem;
	Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
	
	return event;
}

void XMetaCallEvent_handler(XMetaCallEvent* event, XObject* receiver)
{
	if (!event)
		return; 
	/*if (event->sem)
	{
		XPrintf("XMetaCallEvent:%p 出问题了 %p\n", event,event->sem);
	}*/
	if (receiver)
	{
		//队列连接在接收者线程派发,同样用每线程发送者栈设置 sender()
		XThreadData_pushSender(receiver, event->sender, event->signal_id);
		if (event->func)
			event->func(receiver, event->argList);
		XThreadData_popSender();
	}
	if (event->sem)
		XSemaphore_release(event->sem, 1);
	XEvent_accept((XEvent*)event);
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

XKeyEvent* XKeyEvent_create_ex(XMemoryType memory, XEventType type, int key, XKeyboardModifiers modifiers)
{
	XKeyEvent* event = XMemory_malloc(sizeof(XKeyEvent), memory);
	if (!event)
		return NULL;
	XKeyEvent_init(event, type, key, modifiers);
	Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
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
	event->m_autoRepeat = false;
}

int XKeyEvent_key(const XKeyEvent* event)
{
	return event ? event->m_key : 0;
}

XKeyboardModifiers XKeyEvent_modifiers(const XKeyEvent* event)
{
	return event ? event->m_modifiers : XKeyboardModifier_NoModifier;
}

bool XKeyEvent_autoRepeat(const XKeyEvent* event)
{
	return event && event->m_autoRepeat;
}

void XKeyEvent_setAutoRepeat(XKeyEvent* event, bool autoRepeat)
{
	if (event) event->m_autoRepeat = autoRepeat;
}

XMouseEvent* XMouseEvent_create_ex(XMemoryType memory, XEventType type, XMouseButton button,
	XKeyboardModifiers modifiers, XPoint position)
{
	XMouseEvent* event = XMemory_malloc(sizeof(XMouseEvent), memory);
	if (!event)
		return NULL;
	XMouseEvent_init(event, type, button, modifiers, position);
	Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
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
	event->m_buttons = button;
	event->m_modifiers = modifiers;
	event->m_position = position;
}

XMouseButton XMouseEvent_button(const XMouseEvent* event)
{
	return event ? event->m_button : XMouseButton_NoButton;
}

XMouseButton XMouseEvent_buttons(const XMouseEvent* event)
{
	return event ? event->m_buttons : XMouseButton_NoButton;
}

void XMouseEvent_setButtons(XMouseEvent* event, XMouseButton buttons)
{
	if (event) event->m_buttons = buttons;
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

XDeferredDeleteEvent* XDeferredDeleteEvent_create_ex(XMemoryType memory, bool isDelete, int loopLevel, int scopeLevel)
{
	XDeferredDeleteEvent* event = XMemory_malloc(sizeof(XDeferredDeleteEvent), memory);
	if (!event)return NULL;
	XEvent_init(event, XEVENT_TYPE_DEFERRED_DELETE);
	event->isDelete = isDelete;
	event->loopLevel = loopLevel;
	event->scopeLevel = scopeLevel;
	Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
	
	return event;
}

void XDeferredDeleteEvent_handler(XDeferredDeleteEvent* event, XObject* receiver)
{
	if (!event || !receiver)
		return;

	XEvent_accept((XEvent*)event);
	if (event->isDelete)
		XClass_delete_base(receiver);
	else
		XClass_deinit_base(receiver);
}

bool XDeferredDeleteEvent_shouldDeliver(const XDeferredDeleteEvent* event,
	const XThreadData* threadData, bool explicitlyRequested)
{
	if (!event || !threadData) return false;

	int currentLoopLevel = (int)XAtomic_load_size_t(
		&threadData->m_loopLevel, XAtomic_MemoryOrder_Acquire);
	int currentScopeLevel = threadData->m_scopeLevel;
	int eventLevel = event->loopLevel + event->scopeLevel;
	int currentLevel = currentLoopLevel + currentScopeLevel;
	bool postedBeforeOutermostLoop = event->loopLevel == 0;

	return eventLevel > currentLevel
		|| (postedBeforeOutermostLoop && currentLoopLevel > 0)
		|| (explicitlyRequested && eventLevel == currentLevel);
}

XTimerEvent* XTimerEvent_create_ex(XMemoryType memory, XTimerId id)
{
	XTimerEvent* event = XMemory_malloc(sizeof(XTimerEvent), memory);
	
	if (!event)return NULL;
	XEvent_init(event, XEVENT_TYPE_TIMER);
	event->timerId = id;
	Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
	//XPrintf("XTimerEvent:%p 创建 type:%d\n", event,event->m_base.type);
	return event;
}
XTimerId XTimerEvent_timerId(const XTimerEvent* event)
{
	return (event && event->m_base.type== XEVENT_TYPE_TIMER) ?
		((XTimerEvent*)event)->timerId : 0;
}

XEventSockAct* XEventSockAct_create_ex(XMemoryType memory, XFd fd, XSocketActType actType)
{
	XEventSockAct* event = XMemory_malloc(sizeof(XEventSockAct), memory);
	if (!event)return NULL;
	XEvent_init(event, XEVENT_TYPE_SOCK_ACT);
	event->fd = fd;
	event->actType = actType;
	Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
	return event;
}
XEventSockClose* XEventSockClose_create_ex(XMemoryType memory, XFd fd)
{
	XEventSockClose* event = XMemory_malloc(sizeof(XEventSockClose), memory);
	if (!event)return NULL;
	XEvent_init(event, XEVENT_TYPE_SOCK_CLOSE);
	event->fd = fd;
	Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
	return event;
}
XChildEvent* XChildEvent_create_ex(XMemoryType memory, XEventType type, XObject* child)
{
	XChildEvent* event = XMemory_malloc(sizeof(XChildEvent), memory);
	if (!event)return NULL;
	XEvent_init(event, type);
	event->child = child;
	Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
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
	XEvent_accept((XEvent*)event);
}

XDynamicPropertyChangeEvent* XDynamicPropertyChangeEvent_create_ex(XMemoryType memory, const char* name)
{
	(void)memory;
	(void)name;
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

/** @brief XEvent 的 Copy 实现：只复制基类数据区，保持 dest 的类身份。
 *  虚表为空时先按基类初始化（此时 type 会被覆盖为 src 的负载）。 */
static void VXEvent_copy(XEvent* dest, const XEvent* src)
{
	if (ISNULL(dest, "XEvent") || ISNULL(src, "XEvent")) return;
	if (dest == src) return;
	if (XClassIsVtableNull(dest))
		XEvent_init(dest, 0);
	memcpy((char*)dest + sizeof(XClass),
	       (const char*)src + sizeof(XClass),
	       sizeof(XEvent) - sizeof(XClass));
}

/** @brief 基类 Clone 默认实现：只负责分配，复制全部收敛到 Copy（EXClass_Copy）。
 *  分配后继承 src 的虚表保持多态身份，随后经虚表派发复制数据区。 */
XEvent* VXEvent_default_clone(const XEvent* event)
{
	XEvent* copy = (XEvent*)XClass_Malloc(XEvent);
	if (!copy) return NULL;
	XClassGetVtable(copy) = XClassGetVtable(event);
	Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
	Set_Class_IsHeap(copy, true);
	XClass_copy_base(copy, event);
	return copy;
}

/** @brief XKeyEvent 的 Copy 实现：先复制基类部分，再复制按键字段。 */
static void VXKeyEvent_copy(XKeyEvent* dest, const XKeyEvent* src)
{
	if (ISNULL(dest, "XKeyEvent") || ISNULL(src, "XKeyEvent")) return;
	if (dest == src) return;
	XClass_Parent(XEvent, EXClass_Copy, void(*)(XEvent*, const XEvent*))(
		(XEvent*)dest, (const XEvent*)src);
	dest->m_key = src->m_key;
	dest->m_modifiers = src->m_modifiers;
	dest->m_autoRepeat = src->m_autoRepeat;
}

static XEvent* VXKeyEvent_clone(const XKeyEvent* event)
{
	XKeyEvent* copy = XClass_Malloc(XKeyEvent);
	if (!copy) return NULL;
	XClassGetVtable(copy) = XClassGetVtable(event);
	Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
	Set_Class_IsHeap(copy, true);
	XClass_copy_base(copy, event);
	return (XEvent*)copy;
}

/** @brief XMouseEvent 的 Copy 实现：先复制基类部分，再复制鼠标字段。 */
static void VXMouseEvent_copy(XMouseEvent* dest, const XMouseEvent* src)
{
	if (ISNULL(dest, "XMouseEvent") || ISNULL(src, "XMouseEvent")) return;
	if (dest == src) return;
	XClass_Parent(XEvent, EXClass_Copy, void(*)(XEvent*, const XEvent*))(
		(XEvent*)dest, (const XEvent*)src);
	dest->m_button = src->m_button;
	dest->m_buttons = src->m_buttons;
	dest->m_modifiers = src->m_modifiers;
	dest->m_position = src->m_position;
}

static XEvent* VXMouseEvent_clone(const XMouseEvent* event)
{
	XMouseEvent* copy = XClass_Malloc(XMouseEvent);
	if (!copy) return NULL;
	XClassGetVtable(copy) = XClassGetVtable(event);
	Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
	Set_Class_IsHeap(copy, true);
	XClass_copy_base(copy, event);
	return (XEvent*)copy;
}
