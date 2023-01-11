#include"stack.h"
#include"stack_head.h"
#include<string.h>
//char[]数组字符串型入栈
void Stack_Push_charArray(stack* st, const char* x)
{
	STACK* stack=(STACK*)st;
	strcpy(StacketEnlargeCapacity(stack), x);
}
//char[]数组字符串型取元素
char* Stack_top_charArray(stack* st)
{
	return (char*)Stack_top(st);
}