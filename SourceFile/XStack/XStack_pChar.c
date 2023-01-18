#include"XStack.h"
#include"XStack_head.h"
#include<string.h>
//char*型入栈
void XStack_Push_Char(XStack* this_stack, const char* val)
{
	if (isObjectNULL(this_stack, "XStack_Push_Char"))
		return;
	XSTACK* stack=(XSTACK*)this_stack;
	*(char**)StacketEnlargeCapacity(stack) = val;
}
//char*型取元素
char* XStack_top_Char(XStack* this_stack)
{
	if (isObjectNULL(this_stack, "XStack_top_Char"))
		return NULL;
	return *(char**)XStack_top(this_stack);
}