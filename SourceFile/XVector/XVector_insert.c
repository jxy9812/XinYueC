#include"XVector_head.h"
#include<stdlib.h>
#include<string.h>
struct XVector;
void XVector_Push_Back(struct XVector* this_vector, void* x)// 向量尾部增加一个元素X
{
	XVECTOR* v=(XVECTOR*)this_vector;
	VectorEnlargeCapacity(v);
	char* str1 = (char*)v->object._data + v->object._type * v->object._size;
	memcpy(str1, x, v->object._type);
	v->object._size++;
}
void XVector_insert_front(struct XVector* this_vector, const void* p, const void* x)// 向量中指向元素p前增加一个元素x
{
	XVECTOR* v=(XVECTOR*)this_vector;
	VectorEnlargeCapacity(v);
	if (p >= v->front(v) && p <= v->back(v))
	{
		int size = (char*)v->back(v)-(char*)p + v->object._type;
		void* ptr = malloc(size);
		memcpy(ptr, p, size);
		memcpy(p, x, v->object._type);
		memcpy((char*)p + v->object._type, ptr, size);
		v->object._size++;
		free(ptr);
	}
}
void XVector_insert_nfront(struct XVector* this_vector, const void* p, const int n, const void* x)// 向量中指向元素p前增加n个相同的元素x
{
	XVECTOR* v=(XVECTOR*)this_vector;
	if (p >= v->front(v) && p <= v->back(v))
	{
		VectorEnlargeCapacity(v);
		int size = (char*)v->back(v) -(char*)p + v->object._type;
		void* ptr = malloc(size);
		memcpy(ptr, p, size);
		int sizen = ((char*)p - (char*)v->front(v)) / v->object._type;
		for (size_t i = 0; i < n; i++)
		{
			VectorEnlargeCapacity(v);
			memcpy((char*)v->at(v, sizen), x, v->object._type);
			sizen++;
			v->object._size++;
		}
		memcpy((char*)v->at(v, sizen), ptr, size);
		free(ptr);
	}
}
void XVector_insert(struct XVector* this_vector, const void* p, const void* p1, const void* p2)// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2)间的数据
{
	XVECTOR* v=(XVECTOR*)this_vector;
	if (p >= v->front(v) && p <= v->back(v))
	{
		VectorEnlargeCapacity(v);
		int size = (char*)v->back(v) -(char*)p + v->object._type;
		void* ptr = malloc(size);
		memcpy(ptr, p, size);
		int sizen = ((char*)p - (char*)v->front(v)) / v->object._type;
		int push_n = ((char*)p2 - (char*)p1) / v->object._type + 1;
		for (size_t i = 0; i < push_n; i++)
		{
			VectorEnlargeCapacity(v);
			memcpy((char*)v->at(v, sizen), (char*)p1 + i * v->object._type, v->object._type);
			sizen++;
			v->object._size++;
		}
		memcpy((char*)v->at(v, sizen), ptr, size);
		free(ptr);
	}
}