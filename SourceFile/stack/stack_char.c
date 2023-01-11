#include"stack.h"
#include"stack_head.h"
#include<string.h>
//char型入栈
void Stack_Push_char(stack* st, const char x)
{
	STACK* stack=(STACK*)st;
	*(char*)StacketEnlargeCapacity(stack) = x;
	//memcpy(Capacity(st), &x, st->_type);
}
//char型取元素
char Stack_top_char(stack* st)
{
	STACK* stack=(STACK*)st;
	return *(char*)Stack_top(stack);
}