#include"XStack.h"
#include"XStack_head.h"
#include<string.h>
//char[]数组字符串型入栈
void XStack_Push_charArray(XStack* this_stack, const char* val)
{
	if (isObjectNULL(this_stack, "XStack_Push_charArray"))
		return;
	XSTACK* stack=(XSTACK*)this_stack;
	strcpy(StacketEnlargeCapacity(stack), val);
}
//char[]数组字符串型取元素
char* XStack_top_charArray(XStack* this_stack)
{
	if (isObjectNULL(this_stack, "XStack_top_charArray"))
		return;
	return (char*)XStack_top(this_stack);
}