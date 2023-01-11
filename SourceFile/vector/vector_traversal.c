#include"vector_head.h"
#include<stdlib.h>
#include<string.h>
struct vector;
void* Vector_at(const struct vector* vec, int i)// 返回元素的指针
{
	VECTOR* vector=(VECTOR*)vec;
	if (i + 1 > vector->_current)
	{
		return NULL;
	}
	return (void*)((char*)vector->_date + vector->_type * i);
}
void* Vector_front(const struct vector* vec)//返回向量头指针，指向第一个元素
{
	VECTOR* vector=(VECTOR*)vec;
	return vector->_date;
}
void* Vector_back(const struct vector* vec)//返回向量尾指针，指向向量最后一个元素
{
	VECTOR* vector=(VECTOR*)vec;
	char* _date = (char*)vector->_date + vector->_type * (vector->_current - 1);
	return _date;
}
