#include"vector.h"
#include"vector_head.h"
#include<stdlib.h>
#include<string.h>
void* Vector_at(const struct VECTOR* vec, int i)// 返回元素的指针
{
	if (i + 1 > vec->_current)
	{
		return NULL;
	}
	return (void*)((char*)vec->_date + vec->_type * i);
}
void* Vector_front(const struct VECTOR* vec)//返回向量头指针，指向第一个元素
{
	return vec->_date;
}
void* Vector_back(const struct VECTOR* vec)//返回向量尾指针，指向向量最后一个元素
{
	char* _date = (char*)vec->_date + vec->_type * (vec->_current - 1);
	return _date;
}
