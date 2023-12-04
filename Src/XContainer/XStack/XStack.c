#include"XStack.h"
#include<string.h>

//初始化函数
XStack* XStack_init(size_t TypeSize)
{
	return XVector_init(TypeSize);
}

void XStack_free(XStack* this_stack)
{
	XVector_free(this_stack);
}

void XStack_push(XStack* this_stack, void* LpValue)
{
	XVector_push_back(this_stack,LpValue);
}

void XStack_pop(XStack* this_stack)
{
	XVector_pop_back(this_stack);
}

void* XStack_top(XStack* this_stack)
{
	return XVector_back(this_stack);
}

void XStack_clear(XStack* this_stack)
{
	XVector_clear(this_stack);
}

bool XStack_empty(XStack* this_stack)
{
	return XVector_empty(this_stack);
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
