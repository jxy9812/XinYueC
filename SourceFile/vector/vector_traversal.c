#include"vector_head.h"
#include<stdlib.h>
#include<string.h>
struct vector;
void* Vector_at(const struct vector* vec, int i)// 返回元素的指针
{
	VECTOR* vector=(VECTOR*)vec;
	if (i + 1 > vector->object._size)
	{
		return NULL;
	}
	return (void*)((char*)vector->object._data + vector->object._type * i);
}
void* Vector_front(const struct vector* vec)//返回向量头指针，指向第一个元素
{
	VECTOR* vector=(VECTOR*)vec;
	return vector->object._data;
}
void* Vector_back(const struct vector* vec)//返回向量尾指针，指向向量最后一个元素
{
	VECTOR* vector=(VECTOR*)vec;
	char* _data = (char*)vector->object._data + vector->object._type * (vector->object._size - 1);
	return _data;
}
