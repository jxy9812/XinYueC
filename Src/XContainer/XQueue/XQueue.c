#include"XQueue.h"
#if XQueue_ON
#include<stdlib.h>
#include<string.h>
#include"XAlgorithm.h"
static bool VXQueue_isEmpty(const XQueue* this_queue);
static bool VXQueue_isFull(const XQueue* this_queue);
static void VXQueue_clear(XQueue* this_queue);//清空
static size_t VXQueue_size(const XQueue* this_queue);
//插入到队列的队尾
static bool VXQueue_push(XQueue* this_queue, void* pvValue, XCDataCreatMethod dataCreatMethod);
//出队
static void VXQueue_pop(XQueue* this_queue);
// 返回队头元素
static void* VXQueue_top(XQueue* this_queue);
static bool VXQueue_receive(XQueue* this_queue, void* pvBuffer);

static void VXClass_copy(XQueue* object, const XQueue* src);
static void VXClass_move(XQueue* object, XQueue* src);

XQueue* XQueue_create_ex(XMemoryType memory, size_t typeSize)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XQueue* this_queue = XMemory_malloc(sizeof(XQueue), memory);
	XQueue_init(this_queue, typeSize);
	Set_Class_Memory(this_queue, memory); Set_Class_IsHeap(this_queue, true);
	return this_queue;
}

void XQueue_init(XQueue* this_queue, size_t typeSize)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, ""))
		return;
	XListSLinked_init(this_queue, typeSize,false);
	XClassGetVtable(this_queue) = XQueue_class_init();
}

XVtable* XQueue_class_init()
{
	XVTABLE_INIT_DEFAULT_SIZE(XQUEUE_VTABLE_SIZE)
	XCLASS_SET_CLASS_NAME_DEFAULT("XQueue");
	//继承类
	XVTABLE_INHERIT_XCLASS(XContainer);
	void* table[] = { VXQueue_push,VXQueue_pop,VXQueue_top,VXQueue_receive,VXQueue_isFull };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainer_IsEmpty, VXQueue_isEmpty);
	XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXQueue_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXContainer_Size, VXQueue_size);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
	XCLASS_SHOW_SIZE_DEFAULT(XQueue);
	return XVTABLE_DEFAULT;
}

#endif

bool VXQueue_isEmpty(const XQueue* this_queue)
{
	return XVtableGetFunc(XListSLinked_class_init(), EXContainer_IsEmpty, bool (*)(XListSLinked*))(this_queue);
}

bool VXQueue_isFull(const XQueue* this_queue)
{
	return false;
}

void VXQueue_clear(XQueue* this_queue)
{
	XVtableGetFunc(XListSLinked_class_init(), EXContainer_Clear, void (*)(XListSLinked*))(this_queue);
}

size_t VXQueue_size(const XQueue* this_queue)
{
	return XVtableGetFunc(XListSLinked_class_init(), EXContainer_Size, size_t (*)(XListSLinked*))(this_queue);
}

bool VXQueue_push(XQueue* this_queue, void* pvValue, XCDataCreatMethod dataCreatMethod)
{
	return XVtableGetFunc(XListSLinked_class_init(), EXListBase_Push_Back, bool (*)(XListSLinked*,void*, XCDataCreatMethod))(this_queue, pvValue, dataCreatMethod);
}

void VXQueue_pop(XQueue* this_queue)
{
	XVtableGetFunc(XListSLinked_class_init(), EXListBase_Pop_Front, void (*)(XListSLinked*))(this_queue);
}

void* VXQueue_top(XQueue* this_queue)
{
	return XVtableGetFunc(XListSLinked_class_init(), EXListBase_Front, void* (*)(XListSLinked*))(this_queue);
}

bool VXQueue_receive(XQueue* this_queue, void* pvBuffer)
{
	if(XQueue_isEmpty_base(this_queue))
		return false;
	void* val=XQueue_top_base(this_queue);
	memcpy(pvBuffer,val,XContainerTypeSize(this_queue));
	XQueue_pop_base(this_queue);
	return true;
}

void VXClass_copy(XQueue* object, const XQueue* src)
{
	bool target_uninitialized = XClassIsVtableNull(object);
	if (target_uninitialized)
	{
		XQueue_init(object, XContainerTypeSize(src));
		Class_Memory(object) = Class_Memory(src);
	}
	else if (!XQueue_isEmpty_base(object))
	{
		XQueue_clear_base(object);
	}
	XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
	XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
	XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));
	for_each_iterator(src, XListSLinked, it)
	{
		XQueue_push_base(object, XListSLinked_iterator_data(&it));
	}
}

void VXClass_move(XQueue* object, XQueue* src)
{
	XMemory* source_memory = Class_Memory(src);
	bool target_uninitialized = XClassIsVtableNull(object);
	XMemory* target_memory = target_uninitialized ? NULL : Class_Memory(object);
	if (target_uninitialized)
	{
		XQueue_init(object, XContainerTypeSize(src));
	}
	else if (!XQueue_isEmpty_base(object))
	{
		XQueue_clear_base(object);
	}
	XSwap((XClass*)object + 1, (XClass*)src + 1,
		sizeof(XQueue) - sizeof(XClass));
	Class_Memory(object) = source_memory;
	if (!target_uninitialized)
		Class_Memory(src) = target_memory;
}
