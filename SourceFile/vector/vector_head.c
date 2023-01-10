#include"vector_head.h"
#include<stdio.h>
//检测是否需要扩容
void VectorEnlargeCapacity(VECTOR* vec)
{
	if (vec->_size == 0)
	{
		vec->_date = malloc(vec->_type * VECTORNUM);
		if (vec->_date == NULL)
		{
			perror("初始化vector失败");
			exit(-1);
		}
		else
		{
			vec->_size = VECTORNUM;
		}
	}
	else if (vec->_size == vec->_current)//空间已满需要扩容
	{
		void* _date = realloc(vec->_date, vec->_size * vec->_type * 2);
		if (_date == NULL)
		{
			perror("扩容失败vector");
			exit(-1);
		}
		else
		{
			vec->_date = _date;
			vec->_size *= 2;
		}
	}
}