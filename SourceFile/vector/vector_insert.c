#include"vector_head.h"
#include<stdlib.h>
#include<string.h>
struct vector;
void Vector_Push_Back(struct vector* this_vector, void* x)// 向量尾部增加一个元素X
{
	VECTOR* v=(VECTOR*)this_vector;
	VectorEnlargeCapacity(v);
	char* str1 = (char*)v->_date + v->_type * v->_current;
	memcpy(str1, x, v->_type);
	v->_current++;
}
void Vector_insert_front(struct vector* this_vector, const void* p, const void* x)// 向量中指向元素p前增加一个元素x
{
	VECTOR* v=(VECTOR*)this_vector;
	VectorEnlargeCapacity(v);
	if (p >= v->front(v) && p <= v->back(v))
	{
		int size = (char*)v->back(v)-(char*)p + v->_type;
		void* ptr = malloc(size);
		memcpy(ptr, p, size);
		memcpy(p, x, v->_type);
		memcpy((char*)p + v->_type, ptr, size);
		v->_current++;
		free(ptr);
	}
}
void Vector_insert_nfront(struct vector* this_vector, const void* p, const int n, const void* x)// 向量中指向元素p前增加n个相同的元素x
{
	VECTOR* v=(VECTOR*)this_vector;
	if (p >= v->front(v) && p <= v->back(v))
	{
		VectorEnlargeCapacity(v);
		int size = (char*)v->back(v) -(char*)p + v->_type;
		void* ptr = malloc(size);
		memcpy(ptr, p, size);
		int sizen = ((char*)p - (char*)v->front(v)) / v->_type;
		for (size_t i = 0; i < n; i++)
		{
			VectorEnlargeCapacity(v);
			memcpy((char*)v->at(v, sizen), x, v->_type);
			sizen++;
			v->_current++;
		}
		memcpy((char*)v->at(v, sizen), ptr, size);
		free(ptr);
	}
}
void Vector_insert(struct vector* this_vector, const void* p, const void* p1, const void* p2)// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2)间的数据
{
	VECTOR* v=(VECTOR*)this_vector;
	if (p >= v->front(v) && p <= v->back(v))
	{
		VectorEnlargeCapacity(v);
		int size = (char*)v->back(v) -(char*)p + v->_type;
		void* ptr = malloc(size);
		memcpy(ptr, p, size);
		int sizen = ((char*)p - (char*)v->front(v)) / v->_type;
		int push_n = ((char*)p2 - (char*)p1) / v->_type + 1;
		for (size_t i = 0; i < push_n; i++)
		{
			VectorEnlargeCapacity(v);
			memcpy((char*)v->at(v, sizen), (char*)p1 + i * v->_type, v->_type);
			sizen++;
			v->_current++;
		}
		memcpy((char*)v->at(v, sizen), ptr, size);
		free(ptr);
	}
}