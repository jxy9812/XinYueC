#include"XVector_head.h"
#include<stdio.h>
#include<stdlib.h>
//检测是否需要扩容
void VectorEnlargeCapacity(XVECTOR* this_vector)
{
	if (this_vector->object._capacity == 0)
	{
		this_vector->object._data = malloc(this_vector->object._type * VECTORNUM);
		if (this_vector->object._data == NULL)
		{
			perror("初始化vector失败");
			exit(-1);
		}
		else
		{
			this_vector->object._capacity = VECTORNUM;
		}
	}
	else if (this_vector->object._capacity == this_vector->object._size)//空间已满需要扩容
	{
		void* _data = realloc(this_vector->object._data, this_vector->object._capacity * this_vector->object._type * 1.5);
		if (_data == NULL)
		{
			perror("扩容失败vector");
			exit(-1);
		}
		else
		{
			this_vector->object._data = _data;
			this_vector->object._capacity *= 1.5;
		}
	}
}