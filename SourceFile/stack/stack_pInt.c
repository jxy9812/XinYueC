#include"stack.h"
#include"stack_head.h"
#include<string.h>
//int*型入栈
void Stack_Push_Int(STACK* st, const int* x)
{
	*(int**)StacketEnlargeCapacity(st) = x;
	//memcpy(Capacity(st), &x, st->_type);
}
//int*型取元素
int* Stack_top_Int(STACK* st)
{
	return *(int**)Stack_top(st);
}