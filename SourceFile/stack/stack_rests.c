#include"stack.h"
#include"stack_head.h"
#include<stdlib.h>
#include<string.h>
void Stack_clear(stack* st)
{
	STACK* stack=(STACK*)st;
	if (stack->_date != NULL)
	{
		free(stack->_date);
		stack->_date = NULL;
		stack->_current = 0;
		stack->_size = 0;
	}

}
//判断是否为空
bool Stack_empty(stack* st)//检测栈内是否为空，空为真 O(1)
{
	STACK* stack=(STACK*)st;
	return !stack->_current;
}
//当前最大能存储数据量
int Stack_size(stack* st)//返回stack内元素的个数 O(1)
{
	STACK* stack=(STACK*)st;
	return stack->_current;
}
//返回当前stack所能容纳的最大元素值
int Stack_Capacity(stack* st)
{
	STACK* stack=(STACK*)st;
	return stack->_size;
}

void Stack_Copy(stack* st1, const stack* st2)//将st2拷贝到st1
{
	STACK* stack1=(STACK*)st1;
	STACK* stack2=(STACK*)st2;
	free(stack1->_date);
	stack1->_date = malloc(stack2->_current * stack2->_type);
	memcpy(stack1->_date, stack2->_date, stack2->_current * stack2->_type);
	stack1->_size = stack2->_current;
	stack1->_current = stack2->_current;
	stack1->_type = stack2->_type;
}
void Stack_Rcopy(stack* st1, const stack* st2)//将st2逆序拷贝到st1
{
	STACK* stack1=(STACK*)st1;
	STACK* stack2=(STACK*)st2;
	if (stack2->_current == 0)
		return;
	free(stack1->_date);
	stack1->_date = malloc(stack2->_current * stack2->_type);
	stack1->_size = stack2->_current;
	stack1->_current = stack2->_current;
	stack1->_type = stack2->_type;
	for (char* pst2 = (char*)stack2->_date + (stack2->_current - 1) * stack2->_type, *pst1 = stack1->_date; pst2 >= stack2->_date; pst2 -= stack2->_type, pst1 += stack2->_type)
	{
		memcpy(pst1, pst2, stack2->_type);
	}
}
void Stack_Swap(stack* st1, stack* st2)//交换两个栈
{
	STACK* stack1=(STACK*)st1;
	STACK* stack2=(STACK*)st2;
	void* p = stack1->_date;
	stack1->_date = stack2->_date;
	stack2->_date = p;
	int n = stack1->_current;
	stack1->_current = stack2->_current;
	stack2->_current = n;
	n = stack1->size;
	stack1->size = stack2->size;
	stack2->size = n;
}
void Stack_pop(stack* st)//移除栈顶元素 O(1)
{
	STACK* stack=(STACK*)st;
	if (stack->_current > 0)
		stack->_current--;
}

