#include"XStack.h"
#if XStack_ON
#include<string.h>
#include<stdlib.h>
//初始化函数
XStack* XStack_new(size_t typeSize)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XVector* this_stack = XMemory_malloc(sizeof(XVector));
	XStack_init(this_stack, typeSize);
	return this_stack;
}

void XStack_init(XStack* this_stack, size_t typeSize)
{
	if (ISNULL(this_stack, "") || ISNULL(typeSize, ""))
		return;
	XVector_init(this_stack, typeSize);
	XStack_class_init();
	ObjectVtable(this_stack)=XStackVtable;
}

void XStack_free(XStack* this_stack)
{
	XVector_free(this_stack);
}

void XStack_push(XStack* this_stack, void* LpValue)
{
	if (ISNULL(this_stack, "") || ISNULL(ObjectVtable(this_stack), ""))
		return ;
	typedef void (*funcPtr)(XStack*, void*);
	ObjectVirtualFunc(this_stack, EXStack_Push, funcPtr)(this_stack, LpValue);
}

void XStack_pop(XStack* this_stack)
{
	if (ISNULL(this_stack, "") || ISNULL(ObjectVtable(this_stack), ""))
		return ;
	typedef void (*funcPtr)(XStack*);
	ObjectVirtualFunc(this_stack, EXStack_Pop, funcPtr)(this_stack);
}

void* XStack_top(XStack* this_stack)
{
	if (ISNULL(this_stack, "") || ISNULL(ObjectVtable(this_stack), ""))
		return NULL;
	typedef void* (*funcPtr)(XStack*);
	return ObjectVirtualFunc(this_stack, EXStack_Top, funcPtr)(this_stack);
}

void XStack_clear(XStack* this_stack)
{
	XVector_clear(this_stack);
}

bool XStack_empty(XStack* this_stack)
{
	return XVector_isEmpty(this_stack);
}

int XStack_size(XStack* this_stack)
{
	return XVector_size(this_stack);
}

int XStack_capacity(XStack* this_stack)
{
	return XVector_capacity(this_stack);
}

void XStack_swap(XStack* this_stackOne, XStack* this_stackTwo)
{
	XVector_swap(this_stackOne,this_stackTwo);
}

void XStack_copy(XStack* this_stackOne, const XStack* this_stackTwo)
{
	XVector_copy(this_stackOne, this_stackTwo);
}

void XStack_rcopy(XStack* this_stackOne, const XStack* this_stackTwo)
{
	XVector_rcopy(this_stackOne, this_stackTwo);
}

size_t XStack_typeSize(XStack* this_stack)
{
	return XVector_typeSize(this_stack);
}
#endif