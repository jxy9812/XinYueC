#include"stack.h"
#include"stack_head.h"
#include<stdlib.h>
#include<string.h>
void Stack_clear(STACK* st)
{
	if (st->_date != NULL)
	{
		free(st->_date);
		st->_date = NULL;
		st->_current = 0;
		st->_size = 0;
	}

}
//判断是否为空
bool Stack_empty(STACK* st)//检测栈内是否为空，空为真 O(1)
{
	return !st->_current;
}
//当前最大能存储数据量
int Stack_size(STACK* st)//返回stack内元素的个数 O(1)
{
	return st->_current;
}
//返回当前stack所能容纳的最大元素值
int Stack_Capacity(STACK* st)
{
	return st->_size;
}

void Stack_Copy(STACK* st1, const STACK* st2)//将st2拷贝到st1
{
	free(st1->_date);
	st1->_date = malloc(st2->_current * st2->_type);
	memcpy(st1->_date, st2->_date, st2->_current * st2->_type);
	st1->_size = st2->_current;
	st1->_current = st2->_current;
	st1->_type = st2->_type;
}
void Stack_Rcopy(STACK* st1, const STACK* st2)//将st2逆序拷贝到st1
{
	if (st2->_current == 0)
		return;
	free(st1->_date);
	st1->_date = malloc(st2->_current * st2->_type);
	st1->_size = st2->_current;
	st1->_current = st2->_current;
	st1->_type = st2->_type;
	for (char* pst2 = (char*)st2->_date + (st2->_current - 1) * st2->_type, *pst1 = st1->_date; pst2 >= st2->_date; pst2 -= st2->_type, pst1 += st2->_type)
	{
		memcpy(pst1, pst2, st2->_type);
	}
}
void Stack_Swap(STACK* st1, STACK* st2)//交换两个栈
{
	void* p = st1->_date;
	st1->_date = st2->_date;
	st2->_date = p;
	int n = st1->_current;
	st1->_current = st2->_current;
	st2->_current = n;
	n = st1->size;
	st1->size = st2->size;
	st2->size = n;
}
void Stack_pop(STACK* st)//移除栈顶元素 O(1)
{
	if (st->_current > 0)
		st->_current--;
}

