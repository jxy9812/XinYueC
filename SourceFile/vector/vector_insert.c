#include"vector.h"
#include"vector_head.h"
#include<stdlib.h>
#include<string.h>
void Vector_Push_Back(vector* vec, void* x)// 向量尾部增加一个元素X
{
	VECTOR* vector=(VECTOR*)vec;
	VectorEnlargeCapacity(vec);
	char* str1 = (char*)vector->_date + vector->_type * vector->_current;
	memcpy(str1, x, vector->_type);
	vector->_current++;
}
void Vector_insert_front(vector* vec, const void* p, const void* x)// 向量中指向元素p前增加一个元素x
{
	VECTOR* vector=(VECTOR*)vec;
	VectorEnlargeCapacity(vec);
	if (p >= vector->front(vec) && p <= vector->back(vec))
	{
		int size = (char*)vector->back(vector)-(char*)p + vector->_type;
		void* ptr = malloc(size);
		memcpy(ptr, p, size);
		memcpy(p, x, vector->_type);
		memcpy((char*)p + vector->_type, ptr, size);
		vector->_current++;
		free(ptr);
	}
}
void Vector_insert_nfront(vector* vec, const void* p, const int n, const void* x)// 向量中指向元素p前增加n个相同的元素x
{
	VECTOR* vector=(VECTOR*)vec;
	if (p >= vector->front(vec) && p <= vector->back(vec))
	{
		VectorEnlargeCapacity(vec);
		int size = (char*)vector->back(vec) -(char*)p + vector->_type;
		void* ptr = malloc(size);
		memcpy(ptr, p, size);
		int sizen = ((char*)p - (char*)vector->front(vector)) / vector->_type;
		for (size_t i = 0; i < n; i++)
		{
			VectorEnlargeCapacity(vec);
			memcpy((char*)vector->at(vec, sizen), x, vector->_type);
			sizen++;
			vector->_current++;
		}
		memcpy((char*)vector->at(vec, sizen), ptr, size);
		free(ptr);
	}
}
void Vector_insert(vector* vec, const void* p, const void* p1, const void* p2)// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2)间的数据
{
	VECTOR* vector=(VECTOR*)vec;
	if (p >= vector->front(vec) && p <= vector->back(vec))
	{
		VectorEnlargeCapacity(vec);
		int size = (char*)vector->back(vec) -(char*)p + vector->_type;
		void* ptr = malloc(size);
		memcpy(ptr, p, size);
		int sizen = ((char*)p - (char*)vector->front(vector)) / vector->_type;
		int push_n = ((char*)p2 - (char*)p1) / vector->_type + 1;
		for (size_t i = 0; i < push_n; i++)
		{
			VectorEnlargeCapacity(vec);
			memcpy((char*)vector->at(vec, sizen), (char*)p1 + i * vector->_type, vector->_type);
			sizen++;
			vector->_current++;
		}
		memcpy((char*)vector->at(vec, sizen), ptr, size);
		free(ptr);
	}
}