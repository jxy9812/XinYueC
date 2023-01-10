#include"stack.h"
#include"stack_head.h"
#include<string.h>
//char型入栈
void Stack_Push_char(STACK* st, const char x)
{
	*(char*)StacketEnlargeCapacity(st) = x;
	//memcpy(Capacity(st), &x, st->_type);
}
//char型取元素
char Stack_top_char(STACK* st)
{
	return *(char*)Stack_top(st);
}