#include"stack.h"
#include"stack_head.h"
#include<string.h>

//无类型入栈
void Stack_Push(stack* st,const void* x)// 压栈，增加元素 O(1)
{
	STACK* stack=(STACK*)st;
	memcpy(StacketEnlargeCapacity(st), x, stack->object._type);
}
//无类型取元素
void* Stack_top(stack* st)// 取得栈顶元素（但不删除）O(1)
{
	STACK* stack=(STACK*)st;
	char* _data = (char*)stack->object._data + stack->object._type * (stack->object._size - 1);
	return _data;
}