#include"stack.h"
#include"stack_head.h"
#include<string.h>
//int型入栈
void Stack_Push_int(STACK* st, const int x)
{
	*(int*)StacketEnlargeCapacity(st) = x;
}
//int型取元素
int Stack_top_int(STACK* st)
{
	return *(int*)Stack_top(st);
}