#include"stack_head.h"
#include<stdio.h>
#include<stdlib.h>
//检测是否扩容,并返回需要插入的指针
void* StacketEnlargeCapacity(STACK* this_stack)
{
	if (this_stack->object._size == this_stack->object._capacity)//空间已满需要扩容
	{
		void* _data = realloc(this_stack->object._data, this_stack->object._capacity * this_stack->object._type * 2);
		if (_data == NULL)
		{
			perror("扩容失败sttor");
			exit(-1);
		}
		else
		{
			this_stack->object._data = _data;
			this_stack->object._capacity *= 2;
		}
	}
	char* str1 = (char*)this_stack->object._data + this_stack->object._type * this_stack->object._size;
	this_stack->object._size++;
	return str1;
}