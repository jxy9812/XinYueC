#include "XObject.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XThread.h"
#include "XCoreApplication.h"
#include "XEventDispatcher.h"
static void VXObject_poll(XObject* object);
static void VXObject_delete(XObject* object);
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
	void* table[] = { VXObject_poll };
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXObject_delete);
#if SHOWCONTAINERSIZE
	printf("XObject size:%d\n", XVtable_size(XClassVtable));
#endif
	return XVTABLE_DEFAULT;
}

XObject* XObject_create()
{
	XObject* object = XMemory_malloc(sizeof(XObject));
	XObject_init(object);
	return object;
}

void XObject_init(XObject* object)
{
	if (object == NULL)
		return;
	object->m_eventDispatcher = NULL;
	XThread* thread = XThread_currentThread();
	if (thread == NULL)
	{//当前是主线程
		XEventDispatcher_addObject(XCoreApplication_getEventDispatcher(), object);
	}
	else
	{
		XEventDispatcher_addObject(thread->m_eventDispatcher, object);
	}
}

void XObject_poll_base(XObject* object)
{
	if (ISNULL(object, "") || ISNULL(XClassGetVtable(object), ""))
		return;
	XClassGetVirtualFunc(object, EXObject_Poll, void(*)(XObject*))(object);
}

void VXObject_poll(XObject* object)
{
}

void VXObject_delete(XObject* object)
{
	if (object->m_eventDispatcher)
		XEventDispatcher_removeObject(object->m_eventDispatcher,object);
	XMemory_free(object);
}
