#include"XStack.h"
#include"XStack_head.h"
#include<string.h>
//char*型入栈
void XStack_Push_Char(XStack* this_stack, const char* val)
{
	XSTACK* stack=(XSTACK*)this_stack;
	*(char**)StacketEnlargeCapacity(stack) = val;
	//memcpy(Capacity(st), &x, st->_type);
}
//char*型取元素
char* XStack_top_Char(XStack* this_stack)
{
	return *(char**)XStack_top(this_stack);
}