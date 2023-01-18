#include"stack.h"
#include"stack_head.h"
#include<string.h>
//char*型入栈
void Stack_Push_Char(stack* this_stack, const char* val)
{
	STACK* stack=(STACK*)this_stack;
	*(char**)StacketEnlargeCapacity(stack) = val;
	//memcpy(Capacity(st), &x, st->_type);
}
//char*型取元素
char* Stack_top_Char(stack* this_stack)
{
	return *(char**)Stack_top(this_stack);
}