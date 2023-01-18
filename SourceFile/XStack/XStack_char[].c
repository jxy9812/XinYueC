#include"XStack.h"
#include"XStack_head.h"
#include<string.h>
//char[]数组字符串型入栈
void XStack_Push_charArray(XStack* this_stack, const char* val)
{
	XSTACK* stack=(XSTACK*)this_stack;
	strcpy(StacketEnlargeCapacity(stack), val);
}
//char[]数组字符串型取元素
char* XStack_top_charArray(XStack* this_stack)
{
	return (char*)XStack_top(this_stack);
}