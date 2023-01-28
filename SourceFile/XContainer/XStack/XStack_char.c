#include"XStack.h"
#include"XStack_head.h"
#include<string.h>
//char型入栈
void XStack_Push_char(XStack* this_stack, const char val)
{
	if (isObjectNULL(this_stack, "XStack_Push_char"))
		return;
	XSTACK* stack=(XSTACK*)this_stack;
	*(char*)StacketEnlargeCapacity(stack) = val;
}
//char型取元素
char XStack_top_char(XStack* this_stack)
{
	if (isObjectNULL(this_stack, "XStack_top_char"))
		return;
	XSTACK* stack=(XSTACK*)this_stack;
	return *(char*)XStack_top(stack);
}