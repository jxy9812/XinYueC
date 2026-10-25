#include "XPrintf.h"
#include "XObject.h"
#include "XObject_Protected.h"
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
static void VXObject_deinit(XObject* object);
static bool VXObject_event(XObject* self, XEvent* e);
static bool VXObject_eventFilter(XObject* self, XObject* watched, XEvent* event);
static void VXObject_timerEvent(XObject* timer, XTimerEvent* event);
static void VXObject_poll(XObject* object)
{
}

void VXObject_deinit(XObject* object)
{
	if (!object || object->m_was_deleted)
		return;

	object->m_was_deleted = true;
	object->m_block_sig = 0;
	if (XObject_thread(object) == XThread_currentThread())
		XCoreApplication_removePostedEvents(object, XEVENT_TYPE_NONE);
	/* 注销本对象在事件分发器上的全部定时器（对标 Qt ~QObject 经
	 * QAbstractEventDispatcherPrivate::removeTimerInfo 的对象级清理）：
	 * 定时器回调在计时线程异步触发，若对象先亡而轮表项残留，回调将
	 * 向已释放内存 postEvent（实测堆 UAF 崩溃）。分发器必须取本对象
	 * threadData 所属线程的（XObject_eventDispatcher，与
	 * startTimer/killTimer 同源）——XThread 为每个工作线程建独立
	 * dispatcher，XCoreApplication_eventDispatcher 只返回主线程的，
	 * 跨线程对象会注销错表、真表残留悬垂指针。 */
	{
		XAbstractEventDispatcher* disp = XObject_eventDispatcher(object);
		if (disp)
			XAbstractEventDispatcher_unregisterTimers_base(disp, object);
	}
	XObject_destroyed_signal(object);

	object->m_is_deleting_children = true;
	if(object->m_children)
	{
		for (int i = 0; i < (int)XContainerSize(object->m_children); i++)
		{
			XObject** childPtr = (XObject**)XContainerDataAddr(object->m_children);
			XObject* child = childPtr[i];
			if (!child)continue;
			object->m_currentChildBeingDeleted = child;
			childPtr[i] = NULL;
			child->m_parent = NULL;
			if (Class_IsHeap(child))
				XClassDelete(child);
			else
				XClassDeinit(child);
		}
		XClassDelete(object->m_children);
		object->m_children = NULL;
	}
	object->m_currentChildBeingDeleted = NULL;
	object->m_is_deleting_children = false;

	if (object->m_parent) {
		XObject* parent = object->m_parent;
		object->m_parent = NULL;
		if (parent->m_children && !parent->m_is_deleting_children) {
			int index = XVector_indexOf(parent->m_children, &object, 0);
			if (index != -1)
				XVector_remove_base(parent->m_children, index, 1);
			if (index != -1 && object->m_send_child_events && parent->m_receive_child_events) {
				XChildEvent* event = XChildEvent_create(XEVENT_TYPE_CHILD_REMOVED, object);
				if (event) {
					XCoreApplication_sendEvent(parent, (XEvent*)event);
					XClassDelete((XEvent*)event);
				}
			}
		}
	}

	/* 反注册事件过滤器双向登记（防悬挂引用）：本对象作为被观察者时，从各
	 * 过滤器的回登记表 m_watchedObjects 中移除自己；本对象作为过滤器时，从
	 * 各被观察对象的 m_filters 中移除自己。两侧对称维护后，m_filters 中的
	 * 过滤器指针恒为存活对象，事件分发（XCoreApplication_notify）不再触碰
	 * 已释放内存——此前过滤器先亡会在被观察对象表中残留悬挂指针。 */
	if (object->m_filters)
	{
		for (size_t i = 0; i < XContainerSize(object->m_filters); ++i)
		{
			XObject** filterPtr = (XObject**)XContainerDataAddr(object->m_filters);
			XObject* filter = filterPtr[i];
			if (filter && filter->m_watchedObjects)
			{
				int index = XVector_indexOf(filter->m_watchedObjects, &object, 0);
				if (index != -1)
					XVector_remove_base(filter->m_watchedObjects, index, 1);
			}
		}
		XClassDelete(object->m_filters);
		object->m_filters = NULL;
	}
	if (object->m_watchedObjects)
	{
		for (size_t i = 0; i < XContainerSize(object->m_watchedObjects); ++i)
		{
			XObject** watchedPtr = (XObject**)XContainerDataAddr(object->m_watchedObjects);
			XObject* watched = watchedPtr[i];
			if (watched && watched->m_filters)
			{
				int index = XVector_indexOf(watched->m_filters, &object, 0);
				if (index != -1)
					XVector_remove_base(watched->m_filters, index, 1);
			}
		}
		XClassDelete(object->m_watchedObjects);
		object->m_watchedObjects = NULL;
	}
	if (object->m_object_name)
	{
		XClassDelete(object->m_object_name);
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
				XClassDelete(namePtr[i]);
			}
		}
		XClassDelete(object->m_dynamicPropertyNames);
		object->m_dynamicPropertyNames = NULL;
	}
	if (object->m_dynamicPropertyValues)
	{
		XVariant** values = (XVariant**)XContainerDataAddr(object->m_dynamicPropertyValues);
		for (size_t i = 0; i < XContainerSize(object->m_dynamicPropertyValues); ++i) {
			if (values[i])
				XClassDelete(values[i]);
		}
		XClassDelete(object->m_dynamicPropertyValues);
		object->m_dynamicPropertyValues = NULL;
	}
	// Qt 6.8: ~QObjectPrivate() - 释放线程亲和性引用 (对标 thisThreadData->deref())
	// 放在所有子对象/信号槽清理之后: 清理过程可能仍需通过 threadData 访问当前线程。
	// XObject_init 时 ref, moveToThread 时平衡 (ref 新/deref 旧), 析构时此处 deref 收尾。
	XThreadData* td = (XThreadData*)XAtomic_load_uintptr_t(
		(XAtomic_Uintptr*)&object->m_threadData, XAtomic_MemoryOrder_Acquire);
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
	switch (e->m_type)
	{
		case XEVENT_TYPE_TIMER: XObject_timerEvent_base(self, (XTimerEvent*)e); break;
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
	return e->m_accepted;
}

bool VXObject_eventFilter(XObject* self, XObject* watched, XEvent* event)
{
	return false;
}
void VXObject_timerEvent(XObject* self, XTimerEvent* event)
{
	XEvent_accept((XEvent*)event);
}
XVtable* XObject_class_init()
{
	XVTABLE_INIT_DEFAULT(XObject)
	//继承类
	XVTABLE_INHERIT_XCLASS(XClass);
	void* table[] = {
		VXObject_event ,VXObject_eventFilter,
	NULL,NULL,NULL,NULL,VXObject_timerEvent };
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXObject_deinit);
	XCLASS_SHOW_SIZE_DEFAULT(XObject);
	return XVTABLE_DEFAULT;
}

XObject* XObject_create_ex(XMemoryType memory)
{
	XObject* object = XMemory_malloc(sizeof(XObject), memory);
	if (!object)
		return NULL;
	XObject_init(object);
	Set_Class_Memory(object, memory); Set_Class_IsHeap(object, true);
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
	object->m_send_child_events = true;
	object->m_receive_child_events = true;
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
	if (!object || object->m_was_deleted) {
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
		if (index != -1 && prev_parent->m_is_deleting_children) {
			XObject** children = (XObject**)XContainerDataAddr(prev_parent->m_children);
			children[index] = NULL;
		} else if (index != -1) {
			XVector_remove_base(prev_parent->m_children, index, 1);
			if (object->m_send_child_events && prev_parent->m_receive_child_events) {
				XChildEvent* event = XChildEvent_create(XEVENT_TYPE_CHILD_REMOVED, object);
				if (event) {
					XCoreApplication_sendEvent(prev_parent, (XEvent*)event);
					XClassDelete((XEvent*)event);
				}
			}
		}
	}

	if (object->m_receive_parent_events) {
		XEvent* event = XEvent_create(XEVENT_TYPE_PARENT_ABOUT_TO_CHANGE);
		if (event) {
			XCoreApplication_sendEvent(object, event);
			XClassDelete(event);
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
		if (object->m_send_child_events && parent->m_receive_child_events) {
			XChildEvent* event = XChildEvent_create(XEVENT_TYPE_CHILD_ADDED, object);
			if (event) {
				XCoreApplication_sendEvent(parent, (XEvent*)event);
				XClassDelete((XEvent*)event);
			}
		}
	}

	// Qt 6.8: 发送 ParentChange 事件给自身
	if (object->m_receive_parent_events) {
		XEvent* event = XEvent_create(XEVENT_TYPE_PARENT_CHANGE);
		if (event) {
			XCoreApplication_sendEvent(object, event);
			XClassDelete(event);
		}
	}
}

const XVector* XObject_children(const XObject* self)
{
	if(!self)return NULL;
	return self->m_children;
}

// Qt 6.8: QStyleSheetStyle::previousSiblingNode - 父容器子表顺序查找上一个相邻兄弟
// (供 QSS 兄弟组合符 '+'/'~' 匹配使用; 子表空位不跳过, 与 Qt children() 相邻语义一致)
XObject* XObject_previousSibling(const XObject* self)
{
	if (!self || !self->m_parent || !self->m_parent->m_children)
		return NULL;
	int64_t index = XVector_indexOf(self->m_parent->m_children, &self, 0);
	if (index <= 0)
		return NULL;
	XObject** children = (XObject**)XContainerDataAddr(self->m_parent->m_children);
	return children[index - 1];
}

// Qt 6.8: 兄弟节点查询 - 父容器子表顺序查找下一个相邻兄弟
// (供 QSS 兄弟组合符 '+'/'~' 匹配使用; 子表空位不跳过)
XObject* XObject_nextSibling(const XObject* self)
{
	if (!self || !self->m_parent || !self->m_parent->m_children)
		return NULL;
	int64_t index = XVector_indexOf(self->m_parent->m_children, &self, 0);
	if (index < 0 || index + 1 >= (int64_t)XContainerSize(self->m_parent->m_children))
		return NULL;
	XObject** children = (XObject**)XContainerDataAddr(self->m_parent->m_children);
	return children[index + 1];
}

bool XObject_isWidgetType(const XObject* self)
{
	if (!self)return false;
	return self->m_is_widget;
}

bool XObject_isQuickItemType(const XObject* self)
{
	if (!self) return false;
	return self->m_is_quick_item;
}

bool XObject_isWindowType(const XObject* self)
{
	if (!self)return false;
	return self->m_is_window;
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
		XClassDelete(e);
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
			if (events[i].m_event && events[i].m_receiver == object) {
				XPostEvent event = events[i];
				if (XVector_push_back_1_base(&targetData->m_postEventList, &event)) {
					events[i].m_event = NULL;
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
				if (activeEvents[j].m_event && activeEvents[j].m_receiver == object) {
					XPostEvent event = activeEvents[j];
					if (XVector_push_back_1_base(&targetData->m_postEventList, &event)) {
						activeEvents[j].m_event = NULL;
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
	if (!object || object->m_was_deleted) {
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
	if (object->m_is_widget)
		return false;

	XThreadData* currentData = XThreadData_current();
#if XTHREAD_ON
    XThreadData* targetData = target_thread ? XThreadData_get2(target_thread) : NULL;
#else
    XThreadData* targetData = NULL;
#endif
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
	return self->m_block_sig;
}

bool XObject_blockSignals(XObject* self, bool block)
{
	if (!self)return false;
	bool state = XObject_signalsBlocked(self);
	self->m_block_sig = block;
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
	/* 悬空调试残留「if (timerId == 1)」已删（staged diff 可证系调试
	 * 中间态）：它把下一行守卫短路成自己的条件体，使 timerId!=1 时
	 * 无条件走到 unregisterTimer_base——disp 为 NULL 即触发
	 * ArgIsNULL 报错（回归门禁实测 ×4）。恢复守卫语义。 */
	if (!disp)return ;
	XAbstractEventDispatcher_unregisterTimer_base(disp, timerId);
}
// Qt 6.8: threadData.loadRelaxed() (对标 QObjectPrivate::threadData)
XThreadData* XObject_threadData(const XObject* object)
{
	if (!object)
		return NULL;
	return (XThreadData*)XAtomic_load_uintptr_t(
		(XAtomic_Uintptr*)&object->m_threadData, XAtomic_MemoryOrder_Relaxed);
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

	if (object->m_delete_later_called) {
		XMutex_unlock(td->m_mutex);
		return;
	}
	object->m_delete_later_called = true;

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
			object->m_delete_later_called = false;
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

void XObject_emitSignal(XObject* object, size_t signal, XVarList* args, void(*del)(XVarList*), XAtomic_Int32* ref_count, XEventPriority priority)
{
	if(object)
		XSignalSlot_emit(object->m_signalSlot, signal, args,del, ref_count,priority);
}

size_t XObject_signalIdOf(void* (*fn)(XObject*))
{
	/* 以 NULL 调用信号函数，取其返回值——即信号函数在**其定义模块内**的
	 * 真实地址（各 signal 实现约定：self==NULL 时返回 `(void*)(size_t)(自身名)`）。
	 *
	 * 跨 DLL 边界的必要性（2026-10-25 根修）：调用方（可执行文件）侧
	 * `&fn` 得到的是 MSVC 增量链接生成的导入跳板地址，与动态库发射侧使用
	 * 的真实函数地址不等 → signalMap 查不中 → 槽函数静默不触发（表现为
	 * Test 菜单动作全部失效）。经本次调用，跳板被穿透、进入定义模块执行，
	 * 其返回值即定义模块真址，与发射侧一致。
	 *
	 * 安全性：NULL 调用时各信号函数在 if(self)/if(!self) 守卫内短路，
	 * 不会分配参数（XVarList_Create 等实参构造位于 if 函数体内，惰性求值），
	 * 故无副作用、无泄漏。
	 */
	if (!fn)
		return 0;
	return (size_t)fn(NULL);
}

void* XObject_destroyed_signal(XObject* object)
{
	XEmitSignal(object, XObject_destroyed_signal, NULL, NULL, NULL, XEVENT_PRIORITY_LOWEST);
}
static void XObject_objectNameChanged_signal_del(struct XVarList* list)
{
	XVarList_args_1(list, XString*, objectName);
	if (objectName)
		XClassDelete((XClass*)objectName);
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
				XClassDelete((XClass*)name);
			return;
		}
		if (object && object->m_signalSlot)
			XObject_emitSignal(object, (size_t)XObject_objectNameChanged_signal,
				args, XObject_objectNameChanged_signal_del, NULL, XEVENT_PRIORITY_NORMAL);
		else
		{
			XVarList_setArgsDel(args, XObject_objectNameChanged_signal_del);
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

void XObject_installEventFilter(XObject* self, XObject* filterObj)
{
	if (!self || !filterObj)return;

	// Qt 6.8: 线程亲和性检查 - 过滤器和被过滤对象必须在同一线程
	if (XObject_threadData(self) != XObject_threadData(filterObj)) {
		return;
	}

	// 回登记表须先就绪：过滤器记录自己观察的对象，供过滤器先亡时反注册
	if (!filterObj->m_watchedObjects) {
		filterObj->m_watchedObjects = XVector_create(sizeof(XObject*));
		if (!filterObj->m_watchedObjects)return;
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
	if (!XVector_insert_1_base(filters, 0, &filterObj, 1)) // prepend
		return;
	/* 双向登记：把被观察对象写入过滤器回登记表；写失败则回滚本次安装，
	 * 保持 m_filters 与 m_watchedObjects 两侧登记一致 */
	if (XVector_indexOf(filterObj->m_watchedObjects, &self, 0) == -1
		&& !XVector_push_back_1_base(filterObj->m_watchedObjects, &self))
		XVector_remove_base(filters, 0, 1);
}

void XObject_removeEventFilter(XObject* self, XObject* obj)
{
	if (!self || !obj)return;
	XVector* filters = self->m_filters;
	if (!filters) return;
	int index = XVector_indexOf(filters, &obj, 0);
	if (index != -1)
	{
		XVector_remove_base(filters, index, 1);
		// 同步回删过滤器回登记表中的本对象，保持 m_filters/m_watchedObjects 双向一致
		if (obj->m_watchedObjects)
		{
			int backIndex = XVector_indexOf(obj->m_watchedObjects, &self, 0);
			if (backIndex != -1)
				XVector_remove_base(obj->m_watchedObjects, backIndex, 1);
		}
	}
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
	if (options & XFindChildOption_ChildrenRecursively) {
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
		if (options & XFindChildOption_ChildrenRecursively)
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
	XClassGetVirtualFunc(self, EXObject_TimerEvent, void(*)(XObject*, XTimerEvent*))(self, event);
}

void XObject_childEvent_base(XObject* self, XChildEvent* event)
{
	//子类没重载直接退出
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") || !XClassGetVirtualFunc(self, EXObject_ChildEvent, bool))
		return;
	XClassGetVirtualFunc(self, EXObject_ChildEvent, void(*)(XObject*, XChildEvent*))(self, event);
}

void XObject_customEvent_base(XObject* self, XEvent* event)
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
// 使用 XVariant 存储属性值；仅设置成功时所有权转移给对象，失败路径不接管 value，
// 由调用方自行释放（与 XObject.h 的属主契约一致）
bool XObject_setProperty(XObject* self, const XString* name, XVariant* value)
{
	if (!self || !name) return false;

	// 懒初始化动态属性存储
	if (!self->m_dynamicPropertyNames) {
		self->m_dynamicPropertyNames = XVector_Create(XString*);
		self->m_dynamicPropertyValues = XVector_Create(XVariant*);
		if (!self->m_dynamicPropertyNames || !self->m_dynamicPropertyValues) {
			if (self->m_dynamicPropertyNames) XClassDelete(self->m_dynamicPropertyNames);
			if (self->m_dynamicPropertyValues) XClassDelete(self->m_dynamicPropertyValues);
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
			XClassDelete(valPtr[idx]);
		valPtr[idx] = value;
	} else {
		XString* key = XString_create_copy(name);
		if (!key) return false;
		if (!XVector_push_back_1_base(self->m_dynamicPropertyNames, &key)) {
			XClassDelete(key);
			return false;
		}
		if (!XVector_push_back_1_base(self->m_dynamicPropertyValues, &value)) {
			XVector_remove_base(self->m_dynamicPropertyNames,
				(int64_t)XContainerSize(self->m_dynamicPropertyNames) - 1, 1);
			XClassDelete(key);
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
				XClassDelete(values[i]);
			XClassDelete(namePtr[i]);
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
	for (int i = 0; i < depth; i++) XPrintf("    ");
	XPrintf("%s::%s\n",
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

	XPrintf("OBJECT XObject::%s\n",
		self->m_object_name ? XString_c_str(self->m_object_name) : "(unnamed)");

	// 线程信息
	XPrintf("  THREAD: %p\n", (void*)XObject_thread(self));

	// 父对象
	if (self->m_parent) {
		XPrintf("  PARENT: %s\n",
			self->m_parent->m_object_name ? XString_c_str(self->m_parent->m_object_name) : "(unnamed)");
	} else {
		XPrintf("  PARENT: (none)\n");
	}

	// 子对象数量
	int child_count = self->m_children ? (int)XContainerSize(self->m_children) : 0;
	XPrintf("  CHILDREN: %d\n", child_count);

	// 事件过滤器
	int filter_count = self->m_filters ? (int)XContainerSize(self->m_filters) : 0;
	XPrintf("  EVENT FILTERS: %d\n", filter_count);

	// 投递事件计数
	XPrintf("  POSTED EVENTS: %d\n", XAtomic_load_int32(&self->m_posted_events, XAtomic_MemoryOrder_Relaxed));

	// 标志位
	XPrintf("  FLAGS: %s%s%s%s%s%s\n",
		self->m_is_widget ? "Widget " : "",
		self->m_block_sig ? "BlockSig " : "",
		self->m_was_deleted ? "WasDeleted " : "",
		self->m_is_deleting_children ? "DeletingChildren " : "",
		self->m_delete_later_called ? "DeleteLaterCalled " : "",
		self->m_is_window ? "Window " : "");
}
