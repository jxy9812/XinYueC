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
#include "XVariant.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
static void VXObject_poll(XObject* object);
static void VXObject_deinit(XObject* object);
static bool VXObject_event(XObject* self, XEvent* e);
static bool VXObject_eventFilter(XObject* self, XObject* watched, XEvent* event);
static void VXObject_timerEvent(XObject* timer, XTimerEvent* event);
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
		/*VXObject_poll,*/VXObject_event ,VXObject_eventFilter,
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
	if (!object)
		return NULL;
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
	XAtomic_init(object->m_threadData, 0);
	XThreadData* threadData = XThreadData_current();
	if (threadData) {
		XThreadData_ref(threadData);
		XAtomic_store_uintptr_t(&object->m_threadData, (uintptr_t)threadData,
			XAtomic_MemoryOrder_Release);
	}
	object->send_child_events = true;
	object->receive_child_events = true;
	XAtomic_init(object->m_posted_events, 0);
}

const XString* XObject_objectName(const XObject* self)
{
	if (!self)return NULL;
	return self->m_object_name;
}

void XObject_setObjectName(XObject* self, const XString* name)
{
	if (!self || !name)return;
	if (!self->m_object_name) {
		if (XString_length_base(name) == 0)
			return;
		self->m_object_name = XString_create();
		if (!self->m_object_name)
			return;
	}
	if(XString_compare(self->m_object_name, name)!=XCompare_Equality)
	{
		XString_assign(self->m_object_name, name);
		XObject_objectNameChanged_signal(self, self->m_object_name);
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

void XObject_setParent(XObject* object, XObject* parent)
{
	// 健壮性检查：空对象或已删除的对象不应再设置父对象
	if (!object || object->was_deleted) {
		return;
	}

	// 如果新父对象就是当前父对象，直接返回
	if (object->m_parent == parent) {
		return;
	}
	if (object == parent)
		return;
	for (XObject* ancestor = parent; ancestor; ancestor = ancestor->m_parent) {
		if (ancestor == object)
			return;
	}

	XThreadData* objectData = XObject_threadData(object);
	if (XThreadData_current() != objectData)
		return;

	// Qt 6.8: parent 和 child 必须共享同一个 QThreadData。
	if (parent && objectData != XObject_threadData(parent))
		return;


	XObject* prev_parent = object->m_parent;
	if (prev_parent && prev_parent->m_children) {
		int index = XVector_indexOf(prev_parent->m_children, &object, 0);
		if (index != -1 && prev_parent->is_deleting_children) {
			XObject** children = (XObject**)XContainerDataAddr(prev_parent->m_children);
			children[index] = NULL;
		} else if (index != -1) {
			XVector_remove_base(prev_parent->m_children, index, 1);
			if (object->send_child_events && prev_parent->receive_child_events) {
				XChildEvent* event = XChildEvent_create(XEVENT_TYPE_CHILD_REMOVED, object);
				if (event) {
					XCoreApplication_sendEvent(prev_parent, (XEvent*)event);
					XEvent_delete_base((XEvent*)event);
				}
			}
		}
	}

	if (object->receive_parent_events) {
		XEvent* event = XEvent_create(XEVENT_TYPE_PARENT_ABOUT_TO_CHANGE);
		if (event) {
			XCoreApplication_sendEvent(object, event);
			XEvent_delete_base(event);
		}
	}

	// 设置新的父对象
	object->m_parent = parent;

	// 将自己添加到新父对象的 children 列表中
	if (parent) {
		if (!parent->m_children) {
			parent->m_children = XVector_create(sizeof(XObject*));
		}
		if (parent->m_children) {
			XVector_push_back_1_base(parent->m_children, &object);
		}
		if (object->send_child_events && parent->receive_child_events) {
			XChildEvent* event = XChildEvent_create(XEVENT_TYPE_CHILD_ADDED, object);
			if (event) {
				XCoreApplication_sendEvent(parent, (XEvent*)event);
				XEvent_delete_base((XEvent*)event);
			}
		}
	}

	// Qt 6.8: 发送 ParentChange 事件给自身
	if (object->receive_parent_events) {
		XEvent* event = XEvent_create(XEVENT_TYPE_PARENT_CHANGE);
		if (event) {
			XCoreApplication_sendEvent(object, event);
			XEvent_delete_base(event);
		}
	}
}

const XVector* XObject_children(const XObject* self)
{
	if(!self)return NULL;
	return self->m_children;
}

bool XObject_isWidgetType(const XObject* self)
{
	if (!self)return false;
	return self->is_widget;
}

bool XObject_isQuickItemType(const XObject* self)
{
	if (!self) return false;
	return self->is_quick_item;
}

bool XObject_isWindowType(const XObject* self)
{
	if (!self)return false;
	return self->is_window;
}
XObject* XObject_parent(XObject* object)
{
	if (object)
		return object->m_parent;
	return NULL;
}

// Qt 6.8: moveToThread_helper - 递归发送 ThreadChange 事件给对象及其所有子对象
// (对标 QObjectPrivate::moveToThread_helper)
static void XObject_moveToThread_helper(XObject* object)
{
	if (!object) return;

	// 发送 ThreadChange 事件 (对标 Qt 6.8 QEvent::ThreadChange)
	XEvent* e = XEvent_create(XEVENT_TYPE_THREAD_CHANGE);
	if (e) {
		XCoreApplication_sendEvent(object, e);
		XEvent_delete_base(e);
	}

	// 递归处理所有子对象
	if (object->m_children) {
		for_each_iterator(object->m_children, XVector, it) {
			XObject* child = *((XObject**)XVector_iterator_data(&it));
			if (child) {
				XObject_moveToThread_helper(child);
			}
		}
	}
}

static size_t XObject_setThreadData_helper(XObject* object,
	XThreadData* currentData, XThreadData* targetData)
{
	if (!object) return 0;

	size_t moved = 0;
	if (currentData != targetData) {
		XPostEvent* events = (XPostEvent*)XContainerDataAddr(&currentData->m_postEventList);
		size_t eventCount = XContainerSize(&currentData->m_postEventList);
		for (size_t i = 0; i < eventCount; ++i) {
			if (events[i].event && events[i].receiver == object) {
				XPostEvent event = events[i];
				if (XVector_push_back_1_base(&targetData->m_postEventList, &event)) {
					events[i].event = NULL;
					++moved;
				} else
					XThreadData_discardPostedEvent(&events[i]);
			}
		}

		XVector** activeLists = (XVector**)XContainerDataAddr(
			&currentData->m_activePostEventLists);
		size_t activeCount = XContainerSize(&currentData->m_activePostEventLists);
		for (size_t i = 0; i < activeCount; ++i) {
			XVector* active = activeLists[i];
			if (!active) continue;
			XPostEvent* activeEvents = (XPostEvent*)XContainerDataAddr(active);
			size_t activeEventCount = XContainerSize(active);
			for (size_t j = 0; j < activeEventCount; ++j) {
				if (activeEvents[j].event && activeEvents[j].receiver == object) {
					XPostEvent event = activeEvents[j];
					if (XVector_push_back_1_base(&targetData->m_postEventList, &event)) {
						activeEvents[j].event = NULL;
						++moved;
					} else
						XThreadData_discardPostedEvent(&activeEvents[j]);
				}
			}
		}
	}

	XThreadData_ref(targetData);
	XThreadData* oldData = (XThreadData*)XAtomic_exchange_uintptr_t(
		&object->m_threadData, (uintptr_t)targetData, XAtomic_MemoryOrder_AcqRel);
	XThreadData_deref(oldData);
	if (object->m_children) {
		for_each_iterator(object->m_children, XVector, it) {
			XObject* child = *((XObject**)XVector_iterator_data(&it));
			moved += XObject_setThreadData_helper(child, currentData, targetData);
		}
	}
	return moved;
}

bool XObject_moveToThread(XObject* object, XThread* target_thread)
{
	// 健壮性检查
	if (!object || object->was_deleted) {
		return false;
	}

	// Qt compares the QThread stored in threadData, including NULL affinity.
	if (XObject_thread(object) == target_thread) {
		return true;
	}

	// Qt 6.8: 有父对象的对象不能移动线程
	if (object->m_parent != NULL) {
		return false;
	}
	if (object->is_widget)
		return false;

	XThreadData* currentData = XThreadData_current();
	XThreadData* targetData = target_thread ? XThreadData_get2(target_thread) : NULL;
	XThreadData* thisData = XObject_threadData(object);
	if (!currentData || !thisData)
		return false;

	// Qt permits an affinity-less object to be pulled into the current thread.
	if (!thisData->m_thread && currentData == targetData)
		currentData = thisData;
	else if (thisData != currentData)
		return false;

	bool ownsTargetData = false;
	if (!targetData) {
		targetData = XThreadData_create(NULL);
		if (!targetData)
			return false;
		ownsTargetData = true;
	}

	// Qt 6.8: 递归发送 ThreadChange 事件给对象及其所有子对象
	// (对标 QObjectPrivate::moveToThread_helper)
	XObject_moveToThread_helper(object);
	XThreadData_ref(currentData);
	XThreadData* first = (uintptr_t)currentData < (uintptr_t)targetData ? currentData : targetData;
	XThreadData* second = first == currentData ? targetData : currentData;
	XMutex_lock(first->m_mutex);
	XMutex_lock(second->m_mutex);
	size_t moved = XObject_setThreadData_helper(object, currentData, targetData);
	if (moved > 0)
		XAtomic_store_bool(&targetData->m_canWait, false, XAtomic_MemoryOrder_Release);
	XMutex_unlock(second->m_mutex);
	XMutex_unlock(first->m_mutex);
	XThreadData_deref(currentData);
	if (ownsTargetData)
		XThreadData_deref(targetData);

	if (moved > 0 && targetData->m_eventDispatcher)
		XAbstractEventDispatcher_wakeUp_base(targetData->m_eventDispatcher);

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
	if (!disp)return XTIMER_INVALID_ID;
	return XAbstractEventDispatcher_registerTimer(disp, interval*1000000, timerType, self);
}
XTimerId XObject_startTimer_ns(XObject* self, uint64_t interval_ns, XTimerType timerType)
{
	XAbstractEventDispatcher* disp = XObject_eventDispatcher(self);
	if (!disp)return XTIMER_INVALID_ID;
	return XAbstractEventDispatcher_registerTimer(disp, interval_ns, timerType, self);
}
void XObject_killTimer(XObject* self, XTimerId timerId)
{
	XAbstractEventDispatcher* disp = XObject_eventDispatcher(self);
	if (!disp)return ;
	XAbstractEventDispatcher_unregisterTimer_base(disp, timerId);
}
// Qt 6.8: threadData.loadRelaxed() (对标 QObjectPrivate::threadData)
XThreadData* XObject_threadData(const XObject* object)
{
	if (!object)
		return NULL;
	return (XThreadData*)XAtomic_load_uintptr_t(
		(XAtomic_uintptr_t*)&object->m_threadData, XAtomic_MemoryOrder_Relaxed);
}

// Qt 6.8: QObject::thread() -> d_func()->threadData.loadRelaxed()->thread.loadAcquire()
XThread* XObject_thread(const XObject* object)
{
	if (!object)
		return NULL;
	XThreadData* td = XObject_threadData(object);
	return td ? td->m_thread : NULL;
}

// Qt 6.8: QAbstractEventDispatcher via threadData->eventDispatcher
XAbstractEventDispatcher* XObject_eventDispatcher(const XObject* object)
{
	if (!object)
		return NULL;
	XThreadData* td = XObject_threadData(object);
	if (td && td->m_eventDispatcher)
		return td->m_eventDispatcher;
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

static void XObject_scheduleDeferredDelete(XObject* object, bool isDelete)
{
	if (!object) return;

	XThreadData* td = XThreadData_lockPostEventList(object);
	if (!td) return;

	if (object->delete_later_called) {
		XMutex_unlock(td->m_mutex);
		return;
	}
	object->delete_later_called = true;

	int loopLevel = 0;
	int scopeLevel = 0;
	if (td == XThreadData_current()) {
		loopLevel = (int)XAtomic_load_size_t(&td->m_loopLevel, XAtomic_MemoryOrder_Acquire);
		scopeLevel = td->m_scopeLevel;
		if (scopeLevel == 0 && loopLevel != 0)
			scopeLevel = 1;
	}
	XMutex_unlock(td->m_mutex);

	XDeferredDeleteEvent* event = XDeferredDeleteEvent_create(
		isDelete, loopLevel, scopeLevel);
	if (!event) {
		XThreadData* current = XThreadData_lockPostEventList(object);
		if (current) {
			object->delete_later_called = false;
			XMutex_unlock(current->m_mutex);
		}
		return;
	}
	XCoreApplication_postEvent(object, (XEvent*)event, XEVENT_PRIORITY_LOWEST);
}

void XObject_deinitLater(XObject* object)
{
	XObject_scheduleDeferredDelete(object, false);
}

void XObject_deleteLater(XObject* object)
{
	XObject_scheduleDeferredDelete(object, true);
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
		XString_delete_base((XClass*)objectName);
}
void XObject_objectNameChanged_signal(XObject* object, const XString* objectName)
{
	if(objectName)
	{
		XString* name = XString_create_copy(objectName);
		XVarList* args = XVarList_Create(XVar(XString*, name));
		if (!args)
		{
			if (name)
				XString_delete_base((XClass*)name);
			return;
		}
		if (object && object->m_signalSlot)
			XObject_emitSignal(object, (size_t)XObject_objectNameChanged_signal,
				args, objectNameChanged_signal_del, NULL, XEVENT_PRIORITY_NORMAL);
		else
		{
			XVarList_setArgsDel(args, objectNameChanged_signal_del);
			XVarList_delete(args);
		}
	}
	else
	{
		if (object && object->m_signalSlot)
			XObject_emitSignal(object, (size_t)XObject_objectNameChanged_signal,
				NULL, NULL, NULL, XEVENT_PRIORITY_LOWEST);
	}
}

void VXObject_poll(XObject* object)
{
}

void VXObject_deinit(XObject* object)
{
	if (!object || object->was_deleted)
		return;

	object->was_deleted = true;
	object->block_sig = 0;
	if (XObject_thread(object) == XThread_currentThread())
		XCoreApplication_removePostedEvents(object, XEVENT_TYPE_NONE);
	XObject_destroyed_signal(object);

	object->is_deleting_children = true;
	if(object->m_children)
	{
		for (int i = 0; i < (int)XContainerSize(object->m_children); i++)
		{
			XObject** childPtr = (XObject**)XContainerDataAddr(object->m_children);
			XObject* child = childPtr[i];
			if (!child)continue;
			object->currentChildBeingDeleted = child;
			childPtr[i] = NULL;
			child->m_parent = NULL;
			if (Class_MemoryFree(child))
				XClass_delete_base(child);
			else
				XClass_deinit_base(child);
		}
		XVector_delete_base(object->m_children);
		object->m_children = NULL;
	}
	object->currentChildBeingDeleted = NULL;
	object->is_deleting_children = false;

	if (object->m_parent) {
		XObject* parent = object->m_parent;
		object->m_parent = NULL;
		if (parent->m_children && !parent->is_deleting_children) {
			int index = XVector_indexOf(parent->m_children, &object, 0);
			if (index != -1)
				XVector_remove_base(parent->m_children, index, 1);
			if (index != -1 && object->send_child_events && parent->receive_child_events) {
				XChildEvent* event = XChildEvent_create(XEVENT_TYPE_CHILD_REMOVED, object);
				if (event) {
					XCoreApplication_sendEvent(parent, (XEvent*)event);
					XEvent_delete_base((XEvent*)event);
				}
			}
		}
	}

	if (object->m_filters)
	{
		XVector_delete_base(object->m_filters);
		object->m_filters = NULL;
	}
	if (object->m_object_name)
	{
		XString_delete_base(object->m_object_name);
		object->m_object_name = NULL;
	}
	//释放信号与槽
	if (object->m_signalSlot)
	{
		XSignalSlot_delete(object->m_signalSlot);
		object->m_signalSlot = NULL;
	}
	if (object->m_dynamicPropertyNames)
	{
		for (int i = 0; i < (int)XContainerSize(object->m_dynamicPropertyNames); i++) {
			XString** namePtr = (XString**)XContainerDataAddr(object->m_dynamicPropertyNames);
			if (namePtr[i]) {
				XString_delete_base(namePtr[i]);
			}
		}
		XVector_delete_base(object->m_dynamicPropertyNames);
		object->m_dynamicPropertyNames = NULL;
	}
	if (object->m_dynamicPropertyValues)
	{
		XVariant** values = (XVariant**)XContainerDataAddr(object->m_dynamicPropertyValues);
		for (size_t i = 0; i < XContainerSize(object->m_dynamicPropertyValues); ++i) {
			if (values[i])
				XVariant_delete_base(values[i]);
		}
		XVector_delete_base(object->m_dynamicPropertyValues);
		object->m_dynamicPropertyValues = NULL;
	}
	// Qt 6.8: ~QObjectPrivate() - 释放线程亲和性引用 (对标 thisThreadData->deref())
	// 放在所有子对象/信号槽清理之后: 清理过程可能仍需通过 threadData 访问当前线程。
	// XObject_init 时 ref, moveToThread 时平衡 (ref 新/deref 旧), 析构时此处 deref 收尾。
	XThreadData* td = (XThreadData*)XAtomic_load_uintptr_t(
		(XAtomic_uintptr_t*)&object->m_threadData, XAtomic_MemoryOrder_Acquire);
	if (td) {
		XAtomic_store_uintptr_t(&object->m_threadData, (uintptr_t)NULL,
			XAtomic_MemoryOrder_Release);
		XThreadData_deref(td);
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
		case XEVENT_TYPE_META_CALL: XMetaCallEvent_handler(e, self); break;
		case XEVENT_TYPE_FUNC_RUN: XEventFunc_handler( e); break;
		case XEVENT_TYPE_DEFERRED_DELETE: XDeferredDeleteEvent_handler((XDeferredDeleteEvent*)e, self); break;
		case XEVENT_TYPE_THREAD_CHANGE: {
			// Qt 6.8: ThreadChange 事件 - 在新线程中重新注册定时器
			// 当前实现: 标记事件已处理
			XEvent_accept(e);
			break;
		}
	}
	return e->accepted;
}

bool VXObject_eventFilter(XObject* self, XObject* watched, XEvent* event)
{
	return false;
}
void VXObject_timerEvent(XObject* self, XTimerEvent* event)
{
	XEvent_accept((XEvent*)event);
}
void XObject_installEventFilter(XObject* self, XObject* filterObj)
{
	if (!self || !filterObj)return;

	// Qt 6.8: 线程亲和性检查 - 过滤器和被过滤对象必须在同一线程
	if (XObject_threadData(self) != XObject_threadData(filterObj)) {
		return;
	}

	XVector* filters = self->m_filters;
	if (!filters)
	{
		self->m_filters = XVector_create(sizeof(XObject*));
		filters = self->m_filters;
		if (!filters)return;
	}
	
	// Qt 6.8: 如果已存在，先移除再 prepend（最后安装的最先调用）
	int idx = XVector_indexOf(filters, &filterObj, 0);
	if (idx != -1)
		XVector_remove_base(filters, idx, 1);
	XVector_insert_1_base(filters, 0, &filterObj, 1); // prepend
}

void XObject_removeEventFilter(XObject* self, XObject* obj)
{
	if (!self || !obj)return;
	XVector* filters = self->m_filters;
	if (!filters) return;
	int index = XVector_indexOf(filters, &obj, 0);
	if (index != -1)
		XVector_remove_base(filters, index, 1);
}

static bool XObject_nameMatches(const XObject* object, const XString* name)
{
	if (!object) return false;
	if (!name) return true;
	if (!object->m_object_name) return XString_length_base(name) == 0;
	return XString_equals(object->m_object_name, name, XChar_CaseSensitive);
}

static XObject* XObject_findChild_recursive(const XObject* parent, const XString* name,
	XFindChildOption options)
{
	if (!parent || !parent->m_children) return NULL;
	for_each_iterator(parent->m_children, XVector, it) {
		XObject* child = *((XObject**)XVector_iterator_data(&it));
		if (XObject_nameMatches(child, name))
			return child;
	}
	if (options & XFindChildrenRecursively) {
		for_each_iterator(parent->m_children, XVector, it) {
			XObject* child = *((XObject**)XVector_iterator_data(&it));
			XObject* match = XObject_findChild_recursive(child, name, options);
			if (match) return match;
		}
	}
	return NULL;
}

XObject* XObject_findChild(const XObject* self, const XString* name, XFindChildOption options)
{
	return XObject_findChild_recursive(self, name, options);
}

static void XObject_findChildren_recursive(const XObject* parent, const XString* name,
	XFindChildOption options, XObjectList* result)
{
	if (!parent || !parent->m_children || !result) return;
	for_each_iterator(parent->m_children, XVector, it) {
		XObject* child = *((XObject**)XVector_iterator_data(&it));
		if (!child) continue;
		if (XObject_nameMatches(child, name))
			XVector_push_back_1_base(result, &child);
		if (options & XFindChildrenRecursively)
			XObject_findChildren_recursive(child, name, options, result);
	}
}

XObjectList* XObject_findChildren(const XObject* self, const XString* name, XFindChildOption options)
{
	if (!self) return NULL;
	XObjectList* result = XVector_Create(XObject*);
	XObject_findChildren_recursive(self, name, options, result);
	return result;
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

void XObject_timerEvent_base(XObject* self, XTimerEvent* event)
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
	//从当前线程的发送者栈中查找 receiver==self 的最近一次发射,返回其发送者
	//(取代每对象 m_sender,消除跨线程竞争与嵌套发射错乱)
	return XThreadData_currentSender((XObject*)self);
}

int XObject_senderSignalIndex(const XObject* self)
{
	// Qt 6.8: 从当前线程的发送者栈中查找 receiver==self 的最近一次发射,返回其信号索引
	// (对标 QObject::senderSignalIndex)
	return (int)XThreadData_currentSenderSignalIndex((XObject*)self);
}

// Qt 6.8: 动态属性系统 (对标 QObject::setProperty / property / dynamicPropertyNames)
// 使用 XVariant 存储属性值，由对象管理 XVariant 生命周期
bool XObject_setProperty(XObject* self, const XString* name, XVariant* value)
{
	if (!self || !name) return false;

	// 懒初始化动态属性存储
	if (!self->m_dynamicPropertyNames) {
		self->m_dynamicPropertyNames = XVector_Create(XString*);
		self->m_dynamicPropertyValues = XVector_Create(XVariant*);
		if (!self->m_dynamicPropertyNames || !self->m_dynamicPropertyValues) {
			if (self->m_dynamicPropertyNames) XVector_delete_base(self->m_dynamicPropertyNames);
			if (self->m_dynamicPropertyValues) XVector_delete_base(self->m_dynamicPropertyValues);
			self->m_dynamicPropertyNames = NULL;
			self->m_dynamicPropertyValues = NULL;
			return false;
		}
	}

	// 查找是否已有同名属性
	int idx = -1;
	for (int i = 0; i < (int)XContainerSize(self->m_dynamicPropertyNames); i++) {
		XString** namePtr = (XString**)XContainerDataAddr(self->m_dynamicPropertyNames);
		if (namePtr[i] && XString_compare(namePtr[i], name) == XCompare_Equality) {
			idx = i;
			break;
		}
	}

	if (idx >= 0) {
		// 已存在: 更新值
		XVariant** valPtr = (XVariant**)XContainerDataAddr(self->m_dynamicPropertyValues);
		if (valPtr[idx])
			XVariant_delete_base(valPtr[idx]);
		valPtr[idx] = value;
	} else {
		XString* key = XString_create_copy(name);
		if (!key) return false;
		if (!XVector_push_back_1_base(self->m_dynamicPropertyNames, &key)) {
			XString_delete_base(key);
			return false;
		}
		if (!XVector_push_back_1_base(self->m_dynamicPropertyValues, &value)) {
			XVector_remove_base(self->m_dynamicPropertyNames,
				(int64_t)XContainerSize(self->m_dynamicPropertyNames) - 1, 1);
			XString_delete_base(key);
			return false;
		}
	}

	return true;
}

XVariant* XObject_property(const XObject* self, const XString* name)
{
	if (!self || !name || !self->m_dynamicPropertyNames) return NULL;

	XVariant* result = NULL;
	for (int i = 0; i < (int)XContainerSize(self->m_dynamicPropertyNames); i++) {
		XString** namePtr = (XString**)XContainerDataAddr(self->m_dynamicPropertyNames);
		if (namePtr[i] && XString_compare(namePtr[i], name) == XCompare_Equality) {
			XVariant** valPtr = (XVariant**)XContainerDataAddr(self->m_dynamicPropertyValues);
			result = valPtr[i];
			break;
		}
	}
	return result;
}

XVector* XObject_dynamicPropertyNames(const XObject* self)
{
	if (!self || !self->m_dynamicPropertyNames) return NULL;
	// 返回内部指针的副本 (调用者不应修改)
	return self->m_dynamicPropertyNames;
}

void XObject_removeProperty(XObject* self, const XString* name)
{
	if (!self || !name || !self->m_dynamicPropertyNames) return;

	for (int i = 0; i < (int)XContainerSize(self->m_dynamicPropertyNames); i++) {
		XString** namePtr = (XString**)XContainerDataAddr(self->m_dynamicPropertyNames);
		if (namePtr[i] && XString_compare(namePtr[i], name) == XCompare_Equality) {
			XVariant** values = (XVariant**)XContainerDataAddr(self->m_dynamicPropertyValues);
			if (values[i])
				XVariant_delete_base(values[i]);
			XString_delete_base(namePtr[i]);
			XVector_remove_base(self->m_dynamicPropertyNames, i, 1);
			XVector_remove_base(self->m_dynamicPropertyValues, i, 1);
			break;
		}
	}
}

// Qt 6.8: dumpObjectTree - 递归打印对象树 (对标 QObject::dumpObjectTree)
// Qt 6.8 使用 4-空格缩进,格式: ClassName::ObjectName Flags
void XObject_dumpObjectTree(const XObject* self)
{
	if (!self) return;

	// 使用静态 depth 跟踪递归层级
	static int depth = 0;
	(void)depth; // suppress unused warning

	// Qt 6.8: 打印缩进 + 对象名
	for (int i = 0; i < depth; i++) printf("    ");
	printf("%s::%s\n",
		self->m_object_name ? XString_c_str(self->m_object_name) : "(unnamed)",
		""); // flags placeholder

	// 递归打印子对象
	if (self->m_children) {
		int child_count = (int)XContainerSize(self->m_children);
		XObject** childPtr = (XObject**)XContainerDataAddr(self->m_children);
		depth++;
		for (int i = 0; i < child_count; i++) {
			if (childPtr[i]) {
				XObject_dumpObjectTree(childPtr[i]);
			}
		}
		depth--;
	}
}

// Qt 6.8: dumpObjectInfo - 打印对象详细信息 (对标 QObject::dumpObjectInfo)
// Qt 6.8 格式: 先打印 OBJECT ClassName::ObjectName, 再打印 SIGNALS OUT/IN
void XObject_dumpObjectInfo(const XObject* self)
{
	if (!self) return;

	printf("OBJECT XObject::%s\n",
		self->m_object_name ? XString_c_str(self->m_object_name) : "(unnamed)");

	// 线程信息
	printf("  THREAD: %p\n", (void*)XObject_thread(self));

	// 父对象
	if (self->m_parent) {
		printf("  PARENT: %s\n",
			self->m_parent->m_object_name ? XString_c_str(self->m_parent->m_object_name) : "(unnamed)");
	} else {
		printf("  PARENT: (none)\n");
	}

	// 子对象数量
	int child_count = self->m_children ? (int)XContainerSize(self->m_children) : 0;
	printf("  CHILDREN: %d\n", child_count);

	// 事件过滤器
	int filter_count = self->m_filters ? (int)XContainerSize(self->m_filters) : 0;
	printf("  EVENT FILTERS: %d\n", filter_count);

	// 投递事件计数
	printf("  POSTED EVENTS: %d\n", XAtomic_load_int32(&self->m_posted_events, XAtomic_MemoryOrder_Relaxed));

	// 标志位
	printf("  FLAGS: %s%s%s%s%s%s\n",
		self->is_widget ? "Widget " : "",
		self->block_sig ? "BlockSig " : "",
		self->was_deleted ? "WasDeleted " : "",
		self->is_deleting_children ? "DeletingChildren " : "",
		self->delete_later_called ? "DeleteLaterCalled " : "",
		self->is_window ? "Window " : "");
}
