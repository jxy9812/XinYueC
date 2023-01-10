#include"stack.h"
#include"stack_head.h"
#include<string.h>
//char*型入栈
void Stack_Push_Char(STACK* st, const char* x)
{
	*(char**)StacketEnlargeCapacity(st) = x;
	//memcpy(Capacity(st), &x, st->_type);
}
//char*型取元素
char* Stack_top_Char(STACK* st)
{
	return *(char**)Stack_top(st);
}