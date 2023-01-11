#include"vector_head.h"
#include<stdio.h>
#include<stdlib.h>
//检测是否需要扩容
void VectorEnlargeCapacity(VECTOR* this_vector)
{
	if (this_vector->_size == 0)
	{
		this_vector->_data = malloc(this_vector->_type * VECTORNUM);
		if (this_vector->_data == NULL)
		{
			perror("初始化vector失败");
			exit(-1);
		}
		else
		{
			this_vector->_size = VECTORNUM;
		}
	}
	else if (this_vector->_size == this_vector->_current)//空间已满需要扩容
	{
		void* _data = realloc(this_vector->_data, this_vector->_size * this_vector->_type * 2);
		if (_data == NULL)
		{
			perror("扩容失败vector");
			exit(-1);
		}
		else
		{
			this_vector->_data = _data;
			this_vector->_size *= 2;
		}
	}
}