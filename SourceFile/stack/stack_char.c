#include"stack.h"
#include"stack_head.h"
#include<string.h>
//char型入栈
void Stack_Push_char(stack* this_stack, const char val)
{
	STACK* stack=(STACK*)this_stack;
	*(char*)StacketEnlargeCapacity(stack) = val;
	//memcpy(Capacity(st), &x, st->_type);
}
//char型取元素
char Stack_top_char(stack* this_stack)
{
	STACK* stack=(STACK*)this_stack;
	return *(char*)Stack_top(stack);
}