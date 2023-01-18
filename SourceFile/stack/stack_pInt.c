#include"stack.h"
#include"stack_head.h"
#include<string.h>
//int*型入栈
void Stack_Push_Int(stack* this_stack, const int* val)
{
	STACK* stack=(STACK*)this_stack;
	*(int**)StacketEnlargeCapacity(stack) = val;
	//memcpy(Capacity(st), &x, st->_type);
}
//int*型取元素
int* Stack_top_Int(stack* this_stack)
{
	return *(int**)Stack_top(this_stack);
}