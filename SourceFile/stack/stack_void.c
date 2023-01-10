#include"stack.h"
#include"stack_head.h"
#include<string.h>

//无类型入栈
void Stack_Push(STACK* st,const void* x)// 压栈，增加元素 O(1)
{
	memcpy(StacketEnlargeCapacity(st), x, st->_type);
}
//无类型取元素
void* Stack_top(STACK* st)// 取得栈顶元素（但不删除）O(1)
{
	char* _date = (char*)st->_date + st->_type * (st->_current - 1);
	return _date;
}