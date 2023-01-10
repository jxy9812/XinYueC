#include"stack_head.h"
#include<stdio.h>
#include<stdlib.h>
//检测是否扩容,并返回需要插入的指针
void* StacketEnlargeCapacity(STACK* st)
{
	if (st->_size == st->_current)//空间已满需要扩容
	{
		void* _date = realloc(st->_date, st->_size * st->_type * 2);
		if (_date == NULL)
		{
			perror("扩容失败sttor");
			exit(-1);
		}
		else
		{
			st->_date = _date;
			st->_size *= 2;
		}
	}
	char* str1 = (char*)st->_date + st->_type * st->_current;
	st->_current++;
	return str1;
}