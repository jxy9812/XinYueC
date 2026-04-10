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
	XVTABLE_INHERIT_DEFAULT(XClass_class_init());
	void* table[] = { 
		VXObject_poll,VXObject_event ,VXObject_eventFilter,
	NULL,NULL,NULL,NULL,NULL};
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
	SET_CLASS_HEAP(object);
	return object;
}

void XObject_init(XObject* object)
{
	if (object == NULL)
		return;
	memset(((XClass*)object)+1,0,sizeof(XObject)-sizeof(XClass));
	XClass_init(object);
	XClassGetVtable(object) = XObject_class_init();
	
	//object->children=XVector_create(sizeof(XObject*));
	//object->filters=XVector_create(sizeof(XObject*));
}

const char* XObject_objectName(const XObject* self)
{
	if (!self || !self)return NULL;
	return self->object_name;
}

void XObject_setObjectName(XObject* self, const char* name)
{
	if (!self || !self)return;
	if (self->object_name)
	{
		XDelete(self->object_name);
		self->object_name = NULL;
	}
	if (!name) return;
	size_t len = strlen(name);
	if (len == 0)return;
	self->object_name=XMalloc(len+1);
	memcpy(self->object_name,name,len+1);
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

void XObject_setPollingInterval(XObject* object, size_t interval)
{
	if (object == NULL|| XClassGetVirtualFunc(object, EXObject_Poll, void(*)(XObject*))==NULL)
		return;
	////间隔是0的时候关闭轮询
	//if (interval == 0&& object->m_poolTimer)
	//{//关闭轮询
	//	XTimerBase_stop_base(object->m_poolTimer);
	//	//object->m_poolTimer = NULL;
	//	return;
	//}
	//if (object->m_poolTimer == NULL)
	//{
	//	object->m_poolTimer = XTimer_create();
	//	XTimerBase_setAutoDelete(object->m_poolTimer,false);
	//	XTimerBase_setTimerCallback(object->m_poolTimer,XObject_poll_base);
	//	XTimerBase_setUserData(object->m_poolTimer,object);
	//	XTimerBase_setSingleShot(object->m_poolTimer, false);
	//}
	//XTimerBase_setTimeout(object->m_poolTimer, interval);
	//XTimerBase_setInterval(object->m_poolTimer, interval);
	//XTimerBase_start_base(object->m_poolTimer);
}

void XObject_setParent(XObject* object, XObject* parent)
{
	if (!object)return;
	XObject* prev = XObject_parent(object);//上一个父节点
	if (prev == parent)
		return;//重复设置
	if(prev)
		XCoreApplication_postEvent(prev, XChildEvent_create(XEVENT_TYPE_CHILD_REMOVED, object), XEVENT_PRIORITY_NORMAL);
	object->parent = parent;
	if (parent)
		XCoreApplication_postEvent(parent, XChildEvent_create(XEVENT_TYPE_CHILD_ADDED, object), XEVENT_PRIORITY_NORMAL);
	
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

bool XObject_moveToThread(XObject* object, XThread* thread)
{
	//if (!object||object->m_thread==thread)
	//	return false;
	//XEventDispatcher* dispatcher = XObject_eventDispatcher(object);
	////处理剩余的所有事件,防止遗漏
	//if (dispatcher)
	//{
	//	//XEventDispatcher_handler_base(object->m_eventLoop->m_dispatcher);
	//	if (XEventDispatcher_object_move(dispatcher, XThread_dispatcher(thread), object))
	//	{
	//		object->m_thread = thread;
	//		return true;
	//	}
	//}
	return false;
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

XConnection* XObject_connect(XObject* object, size_t signal, XObject* receiver, XSlotFunc slot_func, XConnectionType type)
{
	if(object==NULL || slot_func == NULL)
		return NULL;
	if(object->m_signalSlot==NULL)
		object->m_signalSlot = XSignalSlot_create(object);
	return XSignalSlot_connect(object->m_signalSlot,signal,receiver,slot_func,type);
}

bool XObject_disconnect(XObject* object, size_t signal, XObject* receiver, XSlotFunc slot_func)
{
	if (object == NULL|| slot_func==NULL)
		return false;
	return XSignalSlot_disconnect(object->m_signalSlot, signal, receiver, slot_func);
}

bool XObject_disconnect_conn(XConnection* conn)
{
	if ( conn == NULL)
		return false;
	return XSignalSlot_disconnect_conn(conn);
}

void XObject_deinitLater(XObject* object)
{
	if (object == NULL)return;
	if (object->delete_later_called)
		return;//已经标记为释放了
	//发送释放信号
	XAtomic_fetch_add_int32(&object->m_posted_events, 1);
	XCoreApplication_postEvent(object, XEventDeferredDelete_create(false), XEVENT_PRIORITY_LOWEST);
	object->delete_later_called = true;
}

void XObject_deleteLater(XObject* object)
{
	if (object == NULL)return;
	if (object->delete_later_called)
		return;//已经标记为释放了
	XAtomic_fetch_add_int32(&object->m_posted_events, 1);
	XCoreApplication_postEvent(object, XEventDeferredDelete_create(true), XEVENT_PRIORITY_LOWEST);
	object->delete_later_called = true;
}

void XObject_emitSignal(XObject* object, size_t signal, XVarList* args, void(*del)(XVarList*), XAtomic_int32_t* ref_count, XEventPriority priority)
{
	if(object)
		XSignalSlot_emit(object->m_signalSlot, signal, args,del, ref_count,priority);
}

void XObject_emitSignal_queue(XObject* object, size_t signal, void* args, void(*del)(void*), XAtomic_int32_t* ref_count, XEventPriority priority)
{
	if (object)
		XSignalSlot_emit_queue(object->m_signalSlot, signal, args, del, ref_count,priority);
}

void* XObject_deinit_signal(XObject* object)
{
	XEmitSignal(object, XObject_deinit_signal, NULL, NULL, NULL, XEVENT_PRIORITY_LOWEST);
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
			if (IS_CLASS_HEAP(child))
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
		XDelete(object->object_name);
		object->object_name = NULL;
	}
	//释放信号与槽
	if (object->m_signalSlot)
	{
		XSignalSlot_delete(object->m_signalSlot);
		object->m_signalSlot = NULL;
	}
	if (object->m_poolTimer)
	{
		XTimerBase_delete_base(object->m_poolTimer);
		object->m_poolTimer = NULL;
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
		XVector_push_back_base(filters, &filterObj);
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
				if (child && child->object_name && strcmp(child->object_name, name) == 0)
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
			if (child && child->object_name && strcmp(child->object_name, name) == 0) return child;
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
				if (child && child->object_name && strcmp(child->object_name, name) == 0)
				{
					XVector_push_back_base(list,&child);
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
			if (child && child->object_name && strcmp(child->object_name, name) == 0)
				XVector_push_back_base(list, &child);
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
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") || !XClassGetVirtualFunc(self, EXObject_TimerEvent, bool))
		return;
	XClassGetVirtualFunc(self, EXObject_ConnectNotify, void(*)(XObject*, size_t))(self, signal);
}

void XObject_disconnectNotify_base(XObject * self, size_t signal)
{
	//子类没重载直接退出
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") || !XClassGetVirtualFunc(self, EXObject_TimerEvent, bool))
		return;
	XClassGetVirtualFunc(self, EXObject_DisconnectNotify, void(*)(XObject*, size_t))(self, signal);
}

XObject* XObject_sender(const XObject* self)
{
	return self? self->sender:NULL;
}
