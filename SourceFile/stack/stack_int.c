#include"stack.h"
#include"stack_head.h"
#include<string.h>
//int型入栈
void Stack_Push_int(stack* st, const int x)
{
	STACK* stack=(STACK*)st;
	*(int*)StacketEnlargeCapacity(stack) = x;
}
//int型取元素
int Stack_top_int(stack* st)
{
	STACK* stack=(STACK*)st;
	return *(int*)Stack_top(stack);
}