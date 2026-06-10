#include "XObject.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XThread.h"
#include "XSetBase.h"
#include "XCoreApplication.h"
#include "XSignalSlot.h"
#include "XThreadData.h"
#include "XMapBase.h"
#include "XTimer.h"
#include "XStack.h"
#include <stdarg.h>
#include <string.h>
static void VXObject_poll(XObject* object);
static void VXObject_deinit(XObject* object);
static bool VXObject_event(XObject* self, XEvent* e);
static bool VXObject_eventFilter(XObject* self, XObject* watched, XEvent* event);
static void VXObject_timerEvent(XObject* timer, XEventTimer* event);
XVtable* XObject_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XObject))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_XCLASS(XClass);
	void* table[] = { 
		VXObject_poll,VXObject_event ,VXObject_eventFilter,
	NULL,NULL,NULL,NULL,VXObject_timerEvent };
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXObject_deinit);
#if SHOWCONTAINERSIZE
	printf("XObject size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

XObject* XObject_create()
{
	XObject* object = XNew(XObject);
	XObject_init(object);
	Set_Class_MemoryFree(object, XFree_System);
	return object;
}

void XObject_init(XObject* object)
{
	if (object == NULL)
		return;
	memset(((XClass*)object)+1,0,sizeof(XObject)-sizeof(XClass));
	XClass_init(object);
	XClassGetVtable(object) = XObject_class_init();
	object->m_thread = XThread_currentThread();
}

const XString* XObject_objectName(const XObject* self)
{
	if (!self)return NULL;
	return self->object_name;
}

void XObject_setObjectName(XObject* self, const XString* name)
{
	if (!self || !name)return;
	if (!self->object_name)self->object_name = XString_create();
	if(XString_compare(self->object_name, name)!=XCompare_Equality)
	{
		XString_assign(self->object_name, name);
		XObject_objectNameChanged_signal(self, self->object_name);
	}
}

bool XObject_isSignalConnected(const XObject* self, size_t signal)
{
	if (!self) return false;
	return XSignalSlot_isSignalConnected(self->m_signalSlot,signal);
}

int XObject_receivers(const XObject* self, size_t signal)
{
	if (!self) return 0;
	return XSignalSlot_receivers(self->m_signalSlot, signal);
}

void XObject_poll_base(XObject* object)
{
	if (ISNULL(object, "") || ISNULL(XClassGetVtable(object), ""))
		return;
	XClassGetVirtualFunc(object, EXObject_Poll, void(*)(XObject*))(object);
}

void XObject_setPollTime(XObject* object, size_t interval)
{
	if (object == NULL|| XClassGetVirtualFunc(object, EXObject_Poll, void(*)(XObject*))==NULL)
		return;
	//间隔是0的时候关闭轮询
	if (interval == 0&& object->pollId)
	{//关闭轮询
		XObject_killTimer(object, object->pollId);
		object->pollId = 0;
		return;
	}
	if (!object->pollId)
	{
		object->pollId = XObject_startTimer_ms(object, interval,XTimerType_PreciseTimer);
	}
}

void XObject_setParent(XObject* object, XObject* parent)
{
	// 健壮性检查：空对象或已删除的对象不应再设置父对象
	if (!object || object->was_deleted) {
		return;
	}

	// 如果新父对象就是当前父对象，直接返回
	if (object->parent == parent) {
		return;
	}

 // 父子对象必须在同一线程中。这是 Qt 对象模型的核心规则。
	XThread* current_thread = XThread_currentThread();
	XThread* object_thread = object->m_thread;
	XThread* new_parent_thread = parent ? parent->m_thread : current_thread;

	// 检查当前调用线程是否是 object 所属的线程
	if (current_thread != object_thread) {
		// 在错误的线程中调用 setParent，这是未定义行为，应记录错误并返回
		// （此处可以用您的日志系统替换 printf）
		//fprintf(stderr, "Error: XObject_setParent called from wrong thread for object %p.\n", (void*)object);
		return;
	}

	// 检查新父对象（如果存在）是否与当前对象在同一线程
	if (parent && object_thread != new_parent_thread) {
		//fprintf(stderr, "Error: Cannot set parent, objects are in different threads.\n");
		return;
	}
	// === 线程一致性检查结束 ===


	 // 获取旧的父对象
	XObject* prev_parent = object->parent;
	// 从旧父对象的 children 列表中移除自己
	if (prev_parent && prev_parent->children) {
		// 注意：这里假设 XVector 提供了安全的查找和移除方法
		int index = XVector_indexOf(prev_parent->children, &object, 0);
		if (index != -1) {
			XVector_remove_base(prev_parent->children, index, 1);
		}
		// 向旧父对象发送 CHILD_REMOVED 事件
		XCoreApplication_postEvent(prev_parent, XChildEvent_create(XEVENT_TYPE_CHILD_REMOVED, object), XEVENT_PRIORITY_NORMAL);
	}

	// 设置新的父对象
	object->parent = parent;

	// 将自己添加到新父对象的 children 列表中
	if (parent) {
		if (!parent->children) {
			parent->children = XVector_create(sizeof(XObject*));
		}
		if (parent->children) {
			XVector_push_back_1_base(parent->children, &object);
		}
		// 向新父对象发送 CHILD_ADDED 事件
		XCoreApplication_postEvent(parent, XChildEvent_create(XEVENT_TYPE_CHILD_ADDED, object), XEVENT_PRIORITY_NORMAL);
	}

	//XObject* prev = XObject_parent(object);//上一个父节点
	//if (prev == parent)
	//	return;//重复设置
	//if(prev)
	//	XCoreApplication_postEvent(prev, XChildEvent_create(XEVENT_TYPE_CHILD_REMOVED, object), XEVENT_PRIORITY_NORMAL);
	//object->parent = parent;
	//if (parent)
	//	XCoreApplication_postEvent(parent, XChildEvent_create(XEVENT_TYPE_CHILD_ADDED, object), XEVENT_PRIORITY_NORMAL);
	//
	//object->parent = parent;
}

const XVector* XObject_children(const XObject* self)
{
	if(!self)return NULL;
	return self->children;
}

bool XObject_isWidgetType(const XObject* self)
{
	if (!self)return false;
	return self->is_widget;
}

bool XObject_isWindowType(const XObject* self)
{
	if (!self)return false;
	return self->is_window;
}
XObject* XObject_parent(XObject* object)
{
	if (object)
		return object->parent;
	return NULL;
}

bool XObject_moveToThread(XObject* object, XThread* target_thread)
{
	// 健壮性检查
	if (!object || object->was_deleted) {
		return false;
	}

	// 如果目标线程就是当前线程，无需移动
	if (object->m_thread == target_thread) {
		return true;
	}

	// === 关键健壮性增强：移动规则检查 ===
	XThread* current_caller_thread = XThread_currentThread();
	XThread* object_current_thread = object->m_thread;

	// 规则1: 只能在对象的“亲生”线程（即创建它的线程）中调用 moveToThread
	// 这里我们简化处理，认为“亲生”线程就是当前所属线程，并且调用者必须在此线程中。
	if (current_caller_thread != object_current_thread) {
		//fprintf(stderr, "Error: XObject_moveToThread can only be called from the object's own thread.\n");
		return false;
	}

	// 规则2: 不能移动到一个正在运行的非当前线程
	// (这是一个简化规则，Qt 的规则更复杂，但核心思想是避免在活动线程间移动)
	if (target_thread && target_thread != current_caller_thread && XThread_isRunning(target_thread)) {
		//fprintf(stderr, "Error: Cannot move object to a running thread.\n");
		return false;
	}
	// === 移动规则检查结束 ===

	// === 处理子对象 ===
	// 根据 Qt 的行为，移动一个对象时，其所有子对象也会被自动移动到同一个线程。
	if (object->children) {
		for_each_iterator(object->children, XVector, it) {
			XObject* child = *((XObject**)XVector_iterator_data(&it));
			// 递归移动子对象
			XObject_moveToThread(child, target_thread);
		}
	}
	// === 子对象处理结束 ===

	// 在移动前，处理完当前线程中所有待处理的事件，确保状态一致
	XCoreApplication_processEvents(XEventLoop_AllEvents);

	// 执行移动：更新线程指针
	object->m_thread = target_thread;

	return true;
}

bool XObject_signalsBlocked(const XObject* self)
{
	if(!self)return false;
	return self->block_sig;
}

bool XObject_blockSignals(XObject* self, bool block)
{
	if (!self)return false;
	bool state = XObject_signalsBlocked(self);
	self->block_sig = block;
	return state;
}
XTimerId XObject_startTimer_ms(XObject* self, uint64_t interval, XTimerType timerType)
{
	XAbstractEventDispatcher* disp = XObject_eventDispatcher(self);
	if (!disp)return 0;
	return XAbstractEventDispatcher_registerTimer(disp, interval*1000000, timerType, self);
}
XTimerId XObject_startTimer_ns(XObject* self, uint64_t interval_ns, XTimerType timerType)
{
	XAbstractEventDispatcher* disp = XObject_eventDispatcher(self);
	if (!disp)return 0;
	return XAbstractEventDispatcher_registerTimer(disp, interval_ns, timerType, self);
}
void XObject_killTimer(XObject* self, XTimerId timerId)
{
	XAbstractEventDispatcher* disp = XObject_eventDispatcher(self);
	if (!disp)return ;
	XAbstractEventDispatcher_unregisterTimer_base(disp, timerId);
}
XThread* XObject_thread(XObject* object)
{
	if(!object)
		return NULL;
	return object->m_thread;
}

XAbstractEventDispatcher* XObject_eventDispatcher(XObject* object)
{
	if (!object)
		return NULL;
	if (object->m_thread)
		return object->m_thread->m_data->m_dispatcher;
	return XCoreApplication_eventDispatcher();
}

XConnection* XObject_connect_1(XObject* object, size_t signal, XObject* receiver, XSlotFunc1 slot_func, XConnectionType type)
{
	if(object==NULL || slot_func == NULL)
		return NULL;
	if(object->m_signalSlot==NULL)
		object->m_signalSlot = XSignalSlot_create(object);
	return XSignalSlot_connect1(object->m_signalSlot,signal,receiver,slot_func,type);
}

XConnection* XObject_connect_2(XObject* object, size_t signal, XSlotFunc2 slot_func)
{
	if (object == NULL || slot_func == NULL)
		return NULL;
	if (object->m_signalSlot == NULL)
		object->m_signalSlot = XSignalSlot_create(object);
	return XSignalSlot_connect2(object->m_signalSlot, signal, slot_func);
}

bool XObject_disconnect_1(XObject* object, size_t signal, XObject* receiver, XSlotFunc1 slot_func1)
{
	if (object == NULL|| slot_func1==NULL)
		return false;
	return XSignalSlot_disconnect1(object->m_signalSlot, signal, receiver, slot_func1);
}

bool XObject_disconnect_2(XConnection* conn)
{
	if ( conn == NULL)
		return false;
	return XSignalSlot_disconnect2(conn);
}

void XObject_deinitLater(XObject* object)
{
	if (object == NULL)return;
	if (object->delete_later_called)
		return;//已经标记为释放了
	XObject_setParent(object,NULL);
	//发送释放信号
	XAtomic_fetch_add_int32(&object->m_posted_events, 1, XAtomic_MemoryOrder_Relaxed);
	XCoreApplication_postEvent(object, XEventDeferredDelete_create(false), XEVENT_PRIORITY_LOWEST);
	object->delete_later_called = true;
}

void XObject_deleteLater(XObject* object)
{
	if (object == NULL)return;
	if (object->delete_later_called)
		return;//已经标记为释放了
	XObject_setParent(object, NULL);
	XAtomic_fetch_add_int32(&object->m_posted_events, 1, XAtomic_MemoryOrder_Relaxed);
	XCoreApplication_postEvent(object, XEventDeferredDelete_create(true), XEVENT_PRIORITY_LOWEST);
	object->delete_later_called = true;
}

void XObject_emitSignal(XObject* object, size_t signal, XVarList* args, void(*del)(XVarList*), XAtomic_int32_t* ref_count, XEventPriority priority)
{
	if(object)
		XSignalSlot_emit(object->m_signalSlot, signal, args,del, ref_count,priority);
}

void* XObject_destroyed_signal(XObject* object)
{
	XEmitSignal(object, XObject_destroyed_signal, NULL, NULL, NULL, XEVENT_PRIORITY_LOWEST);
}
static void objectNameChanged_signal_del(struct XVarList* list)
{
	XVarList_args_1(list, XString*, objectName);
	if (objectName)
		XString_delete_base(objectName);
}
void XObject_objectNameChanged_signal(XObject* object, const XString* objectName)
{
	if(objectName)
	{
		XString* name = XString_create_copy(objectName);
		XEmitSignal(object, XObject_objectNameChanged_signal, XVarList_Create(XVar(XString*, name)), objectNameChanged_signal_del, NULL, XEVENT_PRIORITY_NORMAL);
	}
	else
	{
		XEmitSignal(object, XObject_objectNameChanged_signal, NULL, NULL, NULL, XEVENT_PRIORITY_LOWEST);
	}
}

void VXObject_poll(XObject* object)
{
}

void VXObject_deinit(XObject* object)
{
	if(object->children)
	{
		for_each_iterator(object->children, XVector,it)
		{
			XObject* child = *((XObject**)XVector_iterator_data(&it));
			if (!child)continue;
			child->parent = NULL; // 断开链接
			if (Class_MemoryFree(child))
				XObject_deleteLater(child);
			else
				XObject_deinitLater(child);
		}
		XVector_delete_base(object->children);
		object->children = NULL;
	}
	if (object->filters)
	{
		XVector_delete_base(object->filters);
		object->filters = NULL;
	}
	if (object->object_name)
	{
		XString_delete_base(object->object_name);
		object->object_name = NULL;
	}
	//释放信号与槽
	if (object->m_signalSlot)
	{
		XSignalSlot_delete(object->m_signalSlot);
		object->m_signalSlot = NULL;
	}
}
bool VXObject_event(XObject* self, XEvent* e)
{
	if (!self || !e) {
		return false; // 安全防护：空指针直接忽略
	}
	switch (e->type)
	{
		case XEVENT_TYPE_TIMER: XObject_timerEvent_base(self, e); break;
		case XEVENT_TYPE_CHILD_ADDED:
		case XEVENT_TYPE_CHILD_POLISHED:
		case XEVENT_TYPE_CHILD_REMOVED: XChildEvent_handler(e,self); break;
		case XEVENT_TYPE_META_CALL: XEventMetaCall_handler(e, self); break;
		case XEVENT_TYPE_FUNC_RUN: XEventFunc_handler( e); break;
		case XEVENT_TYPE_DEFERRED_DELETE: XEventDeferredDelete_handler(e, self); break;
	}
	return e->accepted;
}

bool VXObject_eventFilter(XObject* self, XObject* watched, XEvent* event)
{
	return false;
}
void VXObject_timerEvent(XObject* self, XEventTimer* event)
{
	if (self->pollId == event->timerId)
	{
		XObject_poll_base(self);
		XEvent_accept(event);
	}
}
void XObject_installEventFilter(XObject* self, XObject* filterObj)
{
	if (!self || !filterObj)return;
	XVector* filters = self->filters;
	if (!filters)
	{
		self->filters = XVector_create(sizeof(XObject*));
		filters = self->filters;
		if (!filters)return;
	}
	
	if (-1 == XVector_indexOf(filters, &filterObj, 0))//确保新父节点没有自己
		XVector_push_back_1_base(filters, &filterObj);
}

void XObject_removeEventFilter(XObject* self, XObject* obj)
{
	if (!self || !obj)return;
	XVector* filters = self->filters;
	XVector_remove_base(filters, XVector_indexOf(filters, &obj, 0), 1);
}

XObject* XObject_findChild(const XObject* self, const char* name, XFindChildOption options)
{
	if(!self||!name||!self->children)return NULL;
	if (options == XFindChildrenRecursively)
	{
		XStack* sk = XStack_Create(XObject*);
		XStack_push_base(sk, &self);
		
		while (!XStack_isEmpty_base(sk))
		{
			XObject* c = XStack_Top_Base(sk, XObject*);
			XStack_pop_base(sk);
			for_each_iterator(c, XVector, it)
			{
				XObject* child = *((XObject**)XVector_iterator_data(&it));
				XStack_push_base(sk, &child);
				if (child && child->object_name &&XString_compare(child->object_name, name) == XCompare_Equality)
				{
					XStack_delete_base(sk);
					return child;
				}
			}
		}
		XStack_delete_base(sk);
	}
	else if (options == XFindDirectChildrenOnly)
	{
		for_each_iterator(self->children, XVector, it)
		{
			XObject* child = *((XObject**)XVector_iterator_data(&it));
			if (child && child->object_name && XString_compare(child->object_name, name) == XCompare_Equality) return child;
		}
	}
	return NULL;
}

XObjectList* XObject_findChildren(const XObject* self, const char* name, XFindChildOption options)
{
	if (!self || !name || !self->children)return NULL;
	XObjectList* list = XVector_Create(XObject*);
	if (options == XFindChildrenRecursively)
	{
		XStack* sk = XStack_Create(XObject*);
		XStack_push_base(sk, &self);

		while (!XStack_isEmpty_base(sk))
		{
			XObject* c = XStack_Top_Base(sk, XObject*);
			XStack_pop_base(sk);
			for_each_iterator(c, XVector, it)
			{
				XObject* child = *((XObject**)XVector_iterator_data(&it));
				XStack_push_base(sk, &child);
				if (child && child->object_name && XString_compare(child->object_name, name) == XCompare_Equality)
				{
					XVector_push_back_1_base(list,&child);
				}
			}
		}
		XStack_delete_base(sk);
	}
	else if (options == XFindDirectChildrenOnly)
	{
		for_each_iterator(self->children, XVector, it)
		{
			XObject* child = *((XObject**)XVector_iterator_data(&it));
			if (child && child->object_name && XString_compare(child->object_name, name) == XCompare_Equality)
				XVector_push_back_1_base(list, &child);
		}
	}
	return list;
}


bool XObject_event_base(XObject* self, XEvent* e)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return false;
	return XClassGetVirtualFunc(self, EXObject_Event, bool(*)(XObject*, XEvent*))(self,e);
}

bool XObject_eventFilter_base(XObject* self, XObject* watched, XEvent* event)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return false;
	return XClassGetVirtualFunc(self, EXObject_EventFilter, bool(*)(XObject*, XObject *, XEvent*))(self, watched, event);
}

void XObject_timerEvent_base(XObject* self, XEventTimer* event)
{
	//子类没重载直接退出
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")|| !XClassGetVirtualFunc(self, EXObject_TimerEvent,bool))
		return ;
	XClassGetVirtualFunc(self, EXObject_TimerEvent, void(*)(XObject*, XEvent*))(self, event);
}

void XObject_childEvent_base(XObject* self, XChildEvent* event)
{
	//子类没重载直接退出
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") || !XClassGetVirtualFunc(self, EXObject_ChildEvent, bool))
		return;
	XClassGetVirtualFunc(self, EXObject_ChildEvent, void(*)(XObject*, XEvent*))(self, event);
}

void XObject_customEvent(XObject* self, XEvent* event)
{
	//子类没重载直接退出
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") || !XClassGetVirtualFunc(self, EXObject_CustomEvent, bool))
		return;
	XClassGetVirtualFunc(self, EXObject_CustomEvent, void(*)(XObject*, XEvent*))(self, event);
}

void XObject_connectNotify_base(XObject* self, size_t signal)
{
	//子类没重载直接退出
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") || !XClassGetVirtualFunc(self, EXObject_ConnectNotify, bool))
		return;
	XClassGetVirtualFunc(self, EXObject_ConnectNotify, void(*)(XObject*, size_t))(self, signal);
}

void XObject_disconnectNotify_base(XObject * self, size_t signal)
{
	//子类没重载直接退出
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") || !XClassGetVirtualFunc(self, EXObject_DisconnectNotify, bool))
		return;
	XClassGetVirtualFunc(self, EXObject_DisconnectNotify, void(*)(XObject*, size_t))(self, signal);
}

XObject* XObject_sender(const XObject* self)
{
	return self? self->sender:NULL;
}
