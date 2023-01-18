#include"stack.h"
#include"stack_head.h"
#include<string.h>
//char[]数组字符串型入栈
void Stack_Push_charArray(stack* this_stack, const char* val)
{
	STACK* stack=(STACK*)this_stack;
	strcpy(StacketEnlargeCapacity(stack), val);
}
//char[]数组字符串型取元素
char* Stack_top_charArray(stack* this_stack)
{
	return (char*)Stack_top(this_stack);
}