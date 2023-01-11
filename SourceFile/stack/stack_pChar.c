#include"stack.h"
#include"stack_head.h"
#include<string.h>
//char*型入栈
void Stack_Push_Char(stack* st, const char* x)
{
	STACK* stack=(STACK*)st;
	*(char**)StacketEnlargeCapacity(stack) = x;
	//memcpy(Capacity(st), &x, st->_type);
}
//char*型取元素
char* Stack_top_Char(stack* st)
{
	return *(char**)Stack_top(st);
}