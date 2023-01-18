#include"stack.h"
#include"stack_head.h"
#include<string.h>
//int型入栈
void Stack_Push_int(stack* this_stack, const int val)
{
	STACK* stack=(STACK*)this_stack;
	*(int*)StacketEnlargeCapacity(stack) = val;
}
//int型取元素
int Stack_top_int(stack* this_stack)
{
	STACK* stack=(STACK*)this_stack;
	return *(int*)Stack_top(stack);
}