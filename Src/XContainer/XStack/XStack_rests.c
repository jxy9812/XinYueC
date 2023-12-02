#include"XStack.h"
#include"XStack_head.h"
#include<stdlib.h>
#include<string.h>
void XStack_clear(XStack* this_stack)
{
	XSTACK* stack=(XSTACK*)this_stack;
	if (isNULL(isNULLInfo(this_stack, "")))
		return;
	if (stack->object._data != NULL)
	{
		free(stack->object._data);
		stack->object._data = NULL;
		stack->object._capacity = 0;
		stack->object._size = 0;
	}

}
//判断是否为空
bool XStack_empty(XStack* this_stack)//检测栈内是否为空，空为真 O(1)
{
	XSTACK* stack=(XSTACK*)this_stack;
	if (isNULL(isNULLInfo(this_stack, "")))
		return true;
	return XContainerObject_empty(&stack->object);
}
//当前最大能存储数据量
int XStack_size(XStack* this_stack)//返回stack内元素的个数 O(1)
{
	XSTACK* stack=(XSTACK*)this_stack;
	if (isNULL(isNULLInfo(this_stack, "")))
		return 0;
	return XContainerObject_size(&stack->object);
}
//返回当前stack所能容纳的最大元素值
int XStack_Capacity(XStack* this_stack)
{
	XSTACK* stack=(XSTACK*)this_stack;
	if (isNULL(isNULLInfo(this_stack, "")))
		return 0;
	return XContainerObject_capacity(&stack->object);
}
size_t XStack_TypeSize(struct XStack* this_stack)
{
	XSTACK* stack = (XSTACK*)this_stack;
	if (isNULL(isNULLInfo(this_stack, "")))
		return 0;
	return stack->object._typeSize;
}
//将st2拷贝到st1
void XStack_Copy(XStack* this_stackOne, const XStack* this_stackTwo)
{
	XSTACK* stack1=(XSTACK*)this_stackOne;
	XSTACK* stack2=(XSTACK*)this_stackTwo;
	if (isNULL(isNULLInfo(stack1, "")) || isNULL(isNULLInfo(stack2, "")))
		return ;
	free(stack1->object._data);
	stack1->object._data = malloc(stack2->object._size * stack2->object._typeSize);
	memcpy(stack1->object._data, stack2->object._data, stack2->object._size * stack2->object._typeSize);
	stack1->object._capacity = stack2->object._size;
	stack1->object._size = stack2->object._size;
	stack1->object._typeSize = stack2->object._typeSize;
}
//将st2逆序拷贝到st1
void XStack_Rcopy(XStack* this_stackOne, const XStack* this_stackTwo)
{
	XSTACK* stack1=(XSTACK*)this_stackOne;
	XSTACK* stack2=(XSTACK*)this_stackTwo;
	if (stack2->object._size == 0)
		return;
	if (isNULL(isNULLInfo(stack1, "")) || isNULL(isNULLInfo(stack2, "")))
		return;
	free(stack1->object._data);
	stack1->object._data = malloc(stack2->object._size * stack2->object._typeSize);
	stack1->object._capacity = stack2->object._size;
	stack1->object._size = stack2->object._size;
	stack1->object._typeSize = stack2->object._typeSize;
	for (char* pst2 = (char*)stack2->object._data + (stack2->object._size - 1) * stack2->object._typeSize, *pst1 = stack1->object._data; pst2 >= stack2->object._data; pst2 -= stack2->object._typeSize, pst1 += stack2->object._typeSize)
	{
		memcpy(pst1, pst2, stack2->object._typeSize);
	}
}
void XStack_Swap(XStack* this_stackOne, XStack* this_stackTwo)//交换两个栈
{
	XSTACK* stack1=(XSTACK*)this_stackOne;
	XSTACK* stack2=(XSTACK*)this_stackTwo;
	if (isNULL(isNULLInfo(stack1, "")) || isNULL(isNULLInfo(stack2, "")))
		return;
	XContainerObject_swap(&stack1->object, &stack2->object);
}
void XStack_free(XStack* this_stack)
{
	if (isNULL(isNULLInfo(this_stack, "")))
		return ;
	XStack_clear(this_stack);
	free(this_stack);
}
void XStack_pop(XStack* this_stack)//移除栈顶元素 O(1)
{
	if (isNULL(isNULLInfo(this_stack, "")))
		return ;
	XSTACK* stack=(XSTACK*)this_stack;
	if (stack->object._size > 0)
		stack->object._size--;
}

