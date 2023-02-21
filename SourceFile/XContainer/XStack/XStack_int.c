#include"XStack.h"
#include"XStack_head.h"
#include<string.h>
//int型入栈
void XStack_Push_int(XStack* this_stack, const int val)
{
	if (isNULL(isNULLInfo(this_stack, "")))
		return;
	XSTACK* stack=(XSTACK*)this_stack;
	*(int*)StacketEnlargeCapacity(stack) = val;
}
//int型取元素
int XStack_top_int(XStack* this_stack)
{
	if (isNULL(isNULLInfo(this_stack, "")))
		return 0;
	XSTACK* stack=(XSTACK*)this_stack;
	return *(int*)XStack_top(stack);
}