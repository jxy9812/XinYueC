#include"XStack.h"
#include"XStack_head.h"
#include<string.h>

//无类型入栈
void XStack_Push(XStack* this_stack,const void* val)// 压栈，增加元素 O(1)
{
	if (isNULL(isNULLInfo(this_stack, "")))
		return;
	XSTACK* stack=(XSTACK*)this_stack;
	memcpy(StacketEnlargeCapacity(this_stack), val, stack->object._type);
}
//无类型取元素
void* XStack_top(XStack* this_stack)// 取得栈顶元素（但不删除）O(1)
{
	if (isNULL(isNULLInfo(this_stack, "")))
		return NULL;
	XSTACK* stack=(XSTACK*)this_stack;
	char* _data = (char*)stack->object._data + stack->object._type * (stack->object._size - 1);
	return _data;
}