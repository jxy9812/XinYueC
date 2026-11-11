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
#include"XByteArray.h"
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
	event->m_type = type;
	/* Qt 6.8：事件**缺省已接受**（`bool m_accept = true;`，qcoreevent.h:344；
	 * 本机实测 `QEvent(QEvent::User).isAccepted()==1`）。X 侧旧实现靠 memset
	 * 归零 ⇒ 缺省"未接受"，与 Qt 相反：「未显式 ignore 即视为已接受」的判定
	 * 两端不同。注意 event() 的**返回值**与 accepted 是两个东西——Qt 的
	 * `QObject::event` 对未识别类型恒返回 false，X「未识别即返回 accepted」
	 * 的旧写法已同步按 Qt 改为返回 false（见 VXObject_event）。 */
	event->m_accepted = true;
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
XMetaCallEvent* XMetaCallEvent_create_ex(XMemoryType memory, XObject* sender,XSlotFunc1 func, size_t signal_id, XVarList* argList,XAtomic_Int32* ref_count,XSemaphore* sem)
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
	event->m_isFunctor = false;
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
		/* ContextReceiver（Qt 的 context 重载）：func 是 XSlotFunc2，首参必须是
		 * **发送者**而不是事件目标（目标只是 context）——此时也**不**把 context
		 * 压进 sender 栈（它不是"正在被调用的接收者"，Qt 里 functor 连接也没有
		 * 接收者上下文）。 */
		if (event->m_isFunctor)
		{
			if (event->func)
				((XSlotFunc2)event->func)(event->sender, event->argList);
		}
		else
		{
			//队列连接在接收者线程派发,同样用每线程发送者栈设置 sender()
			XThreadData_pushSender(receiver, event->sender, event->signal_id);
			if (event->func)
				event->func(receiver, event->argList);
			XThreadData_popSender();
		}
	}
	else if (event->m_isFunctor && event->func)
	{
		((XSlotFunc2)event->func)(event->sender, event->argList);
	}
	if (event->sem)
		XSemaphore_release(event->sem, 1);
	XEvent_accept((XEvent*)event);
}

void XEvent_accept(XEvent* event)
{
	if (event) event->m_accepted = true;
}

XEvent* XEvent_clone_base(const XEvent * event)
{
	if (ISNULL(event, "") || ISNULL(XClassGetVtable(event), ""))
		return NULL;
	return XClassGetVirtualFunc(event, EXEvent_Clone, XEvent*(*)(XEvent*))(event);
}

void XEvent_ignore(XEvent* event)
{
	if (event) event->m_accepted = false;
}

bool XEvent_isAccepted(const XEvent * event)
{
	return event ? event->m_accepted : false;
}

bool XEvent_isInputEvent(const XEvent* event)
{
	return event ? event->m_input_event : false;
}

bool XEvent_isPointerEvent(const XEvent* event)
{
	return event ? event->m_pointer_event : false;
}

bool XEvent_isSinglePointEvent(const XEvent* event)
{
	return event ? event->m_single_point_event : false;
}

void XEvent_setAccepted_base(XEvent* event, bool accepted)
{
	if (ISNULL(event, "") || ISNULL(XClassGetVtable(event), ""))
		return ;
	return XClassGetVirtualFunc(event, EXEvent_SetAccepted, void(*)(XEvent*,bool))(event, accepted);
}

bool XEvent_spontaneous(const XEvent * event)
{
	return event ? event->m_spontaneous : false;
}

XEventType XEvent_type(const XEvent* event)
{
	return event ? event->m_type : XEVENT_TYPE_NONE;
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
	event->m_class.m_input_event = true;
	event->m_key = key;
	event->m_modifiers = modifiers;
	event->m_autoRepeat = false;
	/* 新增负载向后兼容：平台未提供时保持零值（对标 Qt 未知扫描码/时间）。 */
	event->m_nativeScanCode = 0;
	event->m_timestamp = 0;
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

uint32_t XKeyEvent_nativeScanCode(const XKeyEvent* event)
{
	return event ? event->m_nativeScanCode : 0;
}

void XKeyEvent_setNativeScanCode(XKeyEvent* event, uint32_t scanCode)
{
	if (event) event->m_nativeScanCode = scanCode;
}

uint32_t XKeyEvent_timestamp(const XKeyEvent* event)
{
	return event ? event->m_timestamp : 0;
}

void XKeyEvent_setTimestamp(XKeyEvent* event, uint32_t timestamp)
{
	if (event) event->m_timestamp = timestamp;
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
	event->m_class.m_input_event = true;
	event->m_class.m_pointer_event = true;
	event->m_class.m_single_point_event = true;
	event->m_button = button;
	event->m_buttons = button;
	event->m_modifiers = modifiers;
	event->m_position = position;
	event->m_globalPosition.x = 0; /* 平台未提供时保持零（对标 Qt 未知全局坐标）。 */
	event->m_globalPosition.y = 0;
	event->m_synthesized = false; /* 默认真实鼠标设备事件。 */
	event->m_timestamp = 0;       /* 平台未提供时保持零（对标 Qt 未知时间）。 */
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

void XMouseEvent_setSynthesized(XMouseEvent* event, bool synthesized)
{
	if (event) event->m_synthesized = synthesized;
}

bool XMouseEvent_isSynthesized(const XMouseEvent* event)
{
	return event && event->m_synthesized;
}

XPoint XMouseEvent_globalPosition(const XMouseEvent* event)
{
	XPoint position = { 0, 0 };
	return event ? event->m_globalPosition : position;
}

void XMouseEvent_setGlobalPosition(XMouseEvent* event,
	const XPoint* globalPosition)
{
	if (!event)
		return;
	if (globalPosition)
		event->m_globalPosition = *globalPosition;
	else {
		event->m_globalPosition.x = 0;
		event->m_globalPosition.y = 0;
	}
}

uint32_t XMouseEvent_timestamp(const XMouseEvent* event)
{
	return event ? event->m_timestamp : 0;
}

void XMouseEvent_setTimestamp(XMouseEvent* event, uint32_t timestamp)
{
	if (event) event->m_timestamp = timestamp;
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
	/* `m_deferred_delete_is_release`：同一对象先 deinitLater 后 deleteLater 时，
	 * 后到的 deleteLater 只把待处理动作**升级**为释放（不再投递第二个事件），
	 * 故这里以"对象上记录的意图"为准。 */
	if (event->isDelete || receiver->m_deferred_delete_is_release)
		XClassDelete(receiver);
	else
		XClassDeinit(receiver);
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
	//XPrintf("XTimerEvent:%p 创建 type:%d\n", event,event->m_base.m_type);
	return event;
}
XTimerId XTimerEvent_timerId(const XTimerEvent* event)
{
	return (event && event->m_base.m_type== XEVENT_TYPE_TIMER) ?
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
	return event && event->m_type == XEVENT_TYPE_CHILD_ADDED;
}

XObject* XChildEvent_child(const XChildEvent* e)
{
	XEvent* event = (XEvent*)e;
	return (event && (event->m_type == XEVENT_TYPE_CHILD_ADDED ||
		event->m_type == XEVENT_TYPE_CHILD_POLISHED ||
		event->m_type == XEVENT_TYPE_CHILD_REMOVED)) ?
		((XChildEvent*)event)->child : NULL;
}

bool XChildEvent_polished(const XChildEvent* e)
{
	XEvent* event = (XEvent*)e;
	return event && event->m_type == XEVENT_TYPE_CHILD_POLISHED;
}

bool XChildEvent_removed(const XChildEvent* e)
{
	XEvent* event = (XEvent*)e;
	return event && event->m_type == XEVENT_TYPE_CHILD_REMOVED;
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

/* 动态属性变更事件（对标 Qt 6.8 `QDynamicPropertyChangeEvent`）。
 * 事件携带属性名副本（XByteArray，事件拥有），用于 QObject::setProperty /
 * removeProperty 后向对象自身同步派发 QEvent::DynamicPropertyChange。
 * 旧实现恒返回 NULL ⇒ 变更事件根本投递不出去（gaps #11），现在事件有
 * 自己的虚表与 Deinit，`XClassDelete(event)` 会连名字一起释放。 */

/** @brief 属性名副本：**带结尾 '\0'** 的 UTF-8 字节序列（事件拥有）。
 * @details 【2026-11-11 ASan 实锅：heap-buffer-overflow（READ of size 1）
 *          @ Test/QtAlign/XObjectAlignTest.c:187】`XByteArray` 是纯字节序列
 *          （XVector<uint8_t> 的薄封装），**不保证结尾有 '\0'**；而
 *          `XDynamicPropertyChangeEvent_propertyName()` 的返回类型是
 *          `const char*`，调用方按 C 字符串契约使用（测试探针按
 *          `name[i] != 0` 扫描，`strlen/strcmp/XString_create_utf8` 同理）。
 *          旧实现直接把 `XByteArray_data()` 当 C 字符串返回 ⇒ 消费者越过
 *          7 字节内容继续读**未初始化的堆字节**，直到撞 16 字节分配的
 *          ASan 红区（阴影 `=>...:[fa]` 即分配边界）。
 *          Qt 对照：`QDynamicPropertyChangeEvent::propertyName()` 返回
 *          QByteArray，而 QByteArray 的 `constData()` **恒以 '\0' 结尾**
 *          （Qt 6.8 QByteArray 文档：数组始终以 '\0' 结尾），故 Qt 侧按
 *          C 字符串消费是安全的。本仓库没有这条容器级不变量，只能在
 *          **本事件**建副本时显式补一个 '\0'，让 accessor 声明的
 *          `const char*` 契约真正成立。存储内容 = 名字字节 + 结尾 NUL
 *          （属性名不可能含内嵌 NUL：键是 XString 转出的 UTF-8）。 */
static XByteArray* XDynamicPropertyChangeEvent_makeName(const char* name)
{
	size_t len;
	XByteArray* arr;
	if (!name)
		return NULL;
	len = strlen(name);
	arr = XByteArray_create();
	if (!arr)
		return NULL;
	if (len > 0 && !XByteArray_push_back_2(arr, name, len)) {
		XClassDelete((XClass*)arr);
		return NULL;
	}
	if (!XByteArray_push_back_1(arr, (uint8_t)'\0')) {
		XClassDelete((XClass*)arr);
		return NULL;
	}
	return arr;
}

static void VXDynamicPropertyChangeEvent_deinit(XEvent* event)
{
	XDynamicPropertyChangeEvent* e = (XDynamicPropertyChangeEvent*)event;
	if (!e)
		return;
	if (e->propertyName) {
		XClassDelete((XClass*)e->propertyName);
		e->propertyName = NULL;
	}
}

/* 基类缺省 Clone 只按 sizeof(XEvent) 分配（VXEvent_default_clone），而本事件的
 * 类身比 XEvent 大 —— 若沿用基类 Clone，克隆件的 vtable 会指向本类、
 * deinit 却按 XDynamicPropertyChangeEvent 读 propertyName，越界读+误释放。
 * 故自带 Clone（按完整大小分配并复制名字）与 Copy（只复制基类部分，
 * 名字保持目标自己的）。 */
static XVtable* XDynamicPropertyChangeEvent_class_init(void);

static XEvent* VXDynamicPropertyChangeEvent_clone(const XEvent* event)
{
	XDynamicPropertyChangeEvent* copy = NULL;
	const char* name = NULL;
	if (!event)
		return NULL;
	name = XDynamicPropertyChangeEvent_propertyName(event);
	/* 沿源事件自己的内存分配器族分配（与其它事件类的 Clone 同口径）。 */
	copy = (XDynamicPropertyChangeEvent*)Class_Memory(event)->malloc(
		sizeof(XDynamicPropertyChangeEvent));
	if (!copy)
		return NULL;
	XEvent_init(&copy->m_base, XEVENT_TYPE_DYNAMIC_PROPERTY_CHANGE);
	XClassGetVtable(copy) = XDynamicPropertyChangeEvent_class_init();
	Class_Memory(copy) = Class_Memory(event);
	Set_Class_IsHeap(copy, true);
	copy->propertyName = XDynamicPropertyChangeEvent_makeName(name);
	XClassCopy((XClass*)copy, (const XClass*)event);
	return (XEvent*)copy;
}

static void VXDynamicPropertyChangeEvent_copy(XEvent* dest, const XEvent* src)
{
	XClass_Parent(XEvent, EXClass_Copy, void(*)(XEvent*, const XEvent*))(
		(XEvent*)dest, (const XEvent*)src);
}

static XVtable* XDynamicPropertyChangeEvent_class_init(void)
{
	XVTABLE_INIT_DEFAULT_SIZE(XCLASS_VTABLE_GET_SIZE(XEvent))
	/* 类名（对标 metaObject()->className()）；XVTABLE_INIT_DEFAULT_SIZE 不带
	 * 类型参数，故显式登记，便于 XObject_className/inherits 正确识别。 */
	XCLASS_SET_CLASS_NAME_DEFAULT("XDynamicPropertyChangeEvent");
	XVTABLE_INHERIT_XCLASS(XEvent);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXDynamicPropertyChangeEvent_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXDynamicPropertyChangeEvent_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXDynamicPropertyChangeEvent_clone);
	return XVTABLE_DEFAULT;
}

XDynamicPropertyChangeEvent* XDynamicPropertyChangeEvent_create_ex(XMemoryType memory, const char* name)
{
	XDynamicPropertyChangeEvent* event = XMemory_malloc(sizeof(XDynamicPropertyChangeEvent), memory);
	if (!event)
		return NULL;
	XEvent_init(&event->m_base, XEVENT_TYPE_DYNAMIC_PROPERTY_CHANGE);
	XClassGetVtable(event) = XDynamicPropertyChangeEvent_class_init();
	/* 名字副本必须**带结尾 '\0'**（makeName 负责补）：propertyName() 返回
	 * `const char*`，消费者按 C 字符串扫描；XByteArray_create_utf8 只存
	 * strlen 个字节，不带 NUL ⇒ 消费者越过 16 字节分配读红区（2026-11-11
	 * ASan heap-buffer-overflow @ XObjectAlignTest.c:187）。clone 路径已用
	 * makeName，创建路径此前漏改，两处必须同源。 */
	event->propertyName = name ? XDynamicPropertyChangeEvent_makeName(name) : NULL;
	if (name && !event->propertyName)
	{	/* 名字副本分配失败：事件仍可用（propertyName 为 NULL），不泄漏。 */
	}
	Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
	return event;
}

const char* XDynamicPropertyChangeEvent_propertyName(const XEvent* event)
{
	if (event && event->m_type == XEVENT_TYPE_DYNAMIC_PROPERTY_CHANGE) {
		XDynamicPropertyChangeEvent* e = (XDynamicPropertyChangeEvent*)event;
		if (e->propertyName)
			return (const char*)XByteArray_data(e->propertyName);
	}
	return NULL;
}

void VXEvent_default_setAccepted(XEvent* event, bool accepted)
{
	if (event) event->m_accepted = accepted;
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
 *  分配后继承 src 的虚表保持多态身份，随后经虚表派发复制数据区。
 *  @note 克隆件沿源事件自身保存的内存分配器族（Class_Memory）分配与记账，
 *        不再硬编码 XClass_Malloc 的系统堆默认值：源事件在内存池，副本也
 *        留在内存池；源在系统堆，副本也走系统堆。全部事件类统一此口径。 */
XEvent* VXEvent_default_clone(const XEvent* event)
{
	XEvent* copy = (XEvent*)Class_Memory(event)->malloc(sizeof(XEvent));
	if (!copy) return NULL;
	XClassGetVtable(copy) = XClassGetVtable(event);
	Class_Memory(copy) = Class_Memory(event);
	Set_Class_IsHeap(copy, true);
	XClassCopy(copy, event);
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
	dest->m_nativeScanCode = src->m_nativeScanCode;
	dest->m_timestamp = src->m_timestamp;
}

static XEvent* VXKeyEvent_clone(const XKeyEvent* event)
{
	/* 克隆件沿源事件自身保存的内存分配器族分配与记账（同基类口径）。 */
	XKeyEvent* copy = (XKeyEvent*)Class_Memory(event)->malloc(sizeof(XKeyEvent));
	if (!copy) return NULL;
	XClassGetVtable(copy) = XClassGetVtable(event);
	Class_Memory(copy) = Class_Memory(event);
	Set_Class_IsHeap(copy, true);
	XClassCopy(copy, event);
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
	dest->m_globalPosition = src->m_globalPosition;
	dest->m_synthesized = src->m_synthesized;
	dest->m_timestamp = src->m_timestamp;
}

static XEvent* VXMouseEvent_clone(const XMouseEvent* event)
{
	/* 克隆件沿源事件自身保存的内存分配器族分配与记账（同基类口径）。 */
	XMouseEvent* copy = (XMouseEvent*)Class_Memory(event)->malloc(sizeof(XMouseEvent));
	if (!copy) return NULL;
	XClassGetVtable(copy) = XClassGetVtable(event);
	Class_Memory(copy) = Class_Memory(event);
	Set_Class_IsHeap(copy, true);
	XClassCopy(copy, event);
	return (XEvent*)copy;
}
