#include"XVector_head.h"
#include<stdlib.h>
#include<string.h>
struct XVector;
void* XVector_at(const struct XVector* vec, int i)// 返回元素的指针
{
	XVECTOR* vector=(XVECTOR*)vec;
	if (i + 1 > vector->object._size)
	{
		return NULL;
	}
	return (void*)((char*)vector->object._data + vector->object._type * i);
}
void* XVector_front(const struct XVector* vec)//返回向量头指针，指向第一个元素
{
	XVECTOR* vector=(XVECTOR*)vec;
	return vector->object._data;
}
void* XVector_back(const struct XVector* vec)//返回向量尾指针，指向向量最后一个元素
{
	XVECTOR* vector=(XVECTOR*)vec;
	char* _data = (char*)vector->object._data + vector->object._type * (vector->object._size - 1);
	return _data;
}
