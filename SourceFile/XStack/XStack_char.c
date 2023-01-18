#include"XStack.h"
#include"XStack_head.h"
#include<string.h>
//char型入栈
void XStack_Push_char(XStack* this_stack, const char val)
{
	XSTACK* stack=(XSTACK*)this_stack;
	*(char*)StacketEnlargeCapacity(stack) = val;
	//memcpy(Capacity(st), &x, st->_type);
}
//char型取元素
char XStack_top_char(XStack* this_stack)
{
	XSTACK* stack=(XSTACK*)this_stack;
	return *(char*)XStack_top(stack);
}