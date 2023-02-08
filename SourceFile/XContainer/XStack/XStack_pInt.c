#include"XStack.h"
#include"XStack_head.h"
#include<string.h>
//int*型入栈
void XStack_Push_Int(XStack* this_stack, const int* val)
{
	if (isNULL(isNULLInfo(this_stack, "")))
		return;
	XSTACK* stack=(XSTACK*)this_stack;
	*(int**)StacketEnlargeCapacity(stack) = val;
	//memcpy(Capacity(st), &x, st->_type);
}
//int*型取元素
int* XStack_top_Int(XStack* this_stack)
{
	if (isNULL(isNULLInfo(this_stack, "")))
		return NULL;
	return *(int**)XStack_top(this_stack);
}