#include"XStack.h"
#if XStack_ON
#include<string.h>
#include<stdlib.h>
#include"XAlgorithm.h"
static bool VXStack_isEmpty(const XStack* stack);
static bool VXStack_isFull(const XStack* stack);
static void VXStack_clear(XStack* stack);//清空
static size_t VXStack_size(const XStack* stack);
//插入到队列的队尾
static bool VXStack_push(XStack* stack, void* pvValue, XCDataCreatMethod dataCreatMethod);
//出队
static void VXStack_pop(XStack* stack);
// 返回队头元素
static void* VXStack_top(XStack* stack);
static bool VXStack_receive(XStack* stack, void* pvBuffer);

static void VXClass_copy(XStack* object, const XStack* src);
static void VXClass_move(XStack* object, XStack* src);
//初始化函数
XStack* XStack_create(size_t typeSize)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XVector* this_stack = XMalloc_System(sizeof(XVector));
	XStack_init(this_stack, typeSize);
	Set_Class_MemoryFree(this_stack, XFree_System);
	return this_stack;
}

void XStack_init(XStack* this_stack, size_t typeSize)
{
	if (ISNULL(this_stack, "") || ISNULL(typeSize, ""))
		return;
	XVector_init(this_stack, typeSize,false);
	XClassGetVtable(this_stack)= XStack_class_init();
}
bool XStack_resize(XStack* this_stack, size_t new_capacity)
{
	if (ISNULL(this_stack, ""))
		return false;
	size_t size = XStack_size_base(this_stack);
	bool is_ok = XVtableGetFunc(XVector_class_init(), EXVector_Resize,
		bool (*)(XVector*, size_t))(this_stack, new_capacity);
	XContainerSize(this_stack)=size;
	return is_ok;
}
XVtable* XStack_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XSTACK_VTABLE_SIZE)
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_XCLASS(XContainer);
	void* table[] = { VXStack_push,VXStack_pop,VXStack_top,VXStack_receive,VXStack_isFull };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainer_IsEmpty, VXStack_isEmpty);
	XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXStack_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXContainer_Size, VXStack_size);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
#if SHOWCONTAINERSIZE
	printf("XStack size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
#endif

bool VXStack_isEmpty(const XStack* stack)
{
	return XVtableGetFunc(XVector_class_init(), EXContainer_IsEmpty, bool (*)(XVector*))(stack);
}

bool VXStack_isFull(const XStack* stack)
{
	return false;
}

void VXStack_clear(XStack* stack)
{
	XVtableGetFunc(XVector_class_init(), EXContainer_Clear, void (*)(XVector*))(stack);
}

size_t VXStack_size(const XStack * stack)
{
	return XVtableGetFunc(XVector_class_init(), EXContainer_Size, size_t(*)(XVector*))(stack);
}

bool VXStack_push(XStack* stack, void* pvValue, XCDataCreatMethod dataCreatMethod)
{
	return XVtableGetFunc(XVector_class_init(), EXVector_Push_Back, bool (*)(XVector*, void*, XCDataCreatMethod))(stack, pvValue, dataCreatMethod);
}

void VXStack_pop(XStack* stack)
{
	XVtableGetFunc(XVector_class_init(), EXVector_Pop_Back, void (*)(XVector*))(stack);
}

void* VXStack_top(XStack * stack)
{
	return XVtableGetFunc(XVector_class_init(), EXVector_Back, void* (*)(XVector*))(stack);
}

bool VXStack_receive(XStack* stack, void* pvBuffer)
{
	if (XStack_isEmpty_base(stack))
		return false;
	void* val = XStack_top_base(stack);
	memcpy(pvBuffer, val, XContainerTypeSize(stack));
	return true;
}

void VXClass_copy(XStack* object, const XStack* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XStack_init(object, XContainerTypeSize(src));
	}
	else if (!XStack_isEmpty_base(object))
	{
		XStack_clear_base(object);
	}
	XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
	XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
	XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));
	for_each_iterator(src, XVector, it)
	{
		XStack_push_base(object, XVector_iterator_data(&it));
	}
}

void VXClass_move(XStack * object, XStack * src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XStack_init(object, XContainerTypeSize(src));
	}
	else if (!XStack_isEmpty_base(object))
	{
		XStack_clear_base(object);
	}
	XSwap(object, src, sizeof(XStack));
}
