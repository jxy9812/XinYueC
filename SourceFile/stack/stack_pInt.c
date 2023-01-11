#include"stack.h"
#include"stack_head.h"
#include<string.h>
//int*型入栈
void Stack_Push_Int(stack* st, const int* x)
{
	STACK* stack=(STACK*)st;
	*(int**)StacketEnlargeCapacity(stack) = x;
	//memcpy(Capacity(st), &x, st->_type);
}
//int*型取元素
int* Stack_top_Int(stack* st)
{
	return *(int**)Stack_top(st);
}