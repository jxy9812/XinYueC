#include"vector.h"
#include"vector_head.h"
#include<stdlib.h>
#include<string.h>
void Vector_Push_Back(VECTOR* vec, void* x)// 向量尾部增加一个元素X
{
	VectorEnlargeCapacity(vec);
	char* str1 = (char*)vec->_date + vec->_type * vec->_current;
	memcpy(str1, x, vec->_type);
	vec->_current++;
}
void Vector_insert_front(VECTOR* vec, const void* p, const void* x)// 向量中指向元素p前增加一个元素x
{
	VectorEnlargeCapacity(vec);
	if (p >= vec->front(vec) && p <= vec->back(vec))
	{
		int size = (char*)vec->back(vec) - p + vec->_type;
		void* ptr = malloc(size);
		memcpy(ptr, p, size);
		memcpy(p, x, vec->_type);
		memcpy((char*)p + vec->_type, ptr, size);
		vec->_current++;
		free(ptr);
	}
}
void Vector_insert_nfront(VECTOR* vec, const void* p, const int n, const void* x)// 向量中指向元素p前增加n个相同的元素x
{
	if (p >= vec->front(vec) && p <= vec->back(vec))
	{
		VectorEnlargeCapacity(vec);
		int size = (char*)vec->back(vec) - p + vec->_type;
		void* ptr = malloc(size);
		memcpy(ptr, p, size);
		int sizen = ((char*)p - vec->front(vec)) / vec->_type;
		for (size_t i = 0; i < n; i++)
		{
			VectorEnlargeCapacity(vec);
			memcpy((char*)vec->at(vec, sizen), x, vec->_type);
			sizen++;
			vec->_current++;
		}
		memcpy((char*)vec->at(vec, sizen), ptr, size);
		free(ptr);
	}
}
void Vector_insert(VECTOR* vec, const void* p, const void* p1, const void* p2)// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2)间的数据
{
	if (p >= vec->front(vec) && p <= vec->back(vec))
	{
		VectorEnlargeCapacity(vec);
		int size = (char*)vec->back(vec) - p + vec->_type;
		void* ptr = malloc(size);
		memcpy(ptr, p, size);
		int sizen = ((char*)p - vec->front(vec)) / vec->_type;
		int push_n = ((char*)p2 - (char*)p1) / vec->_type + 1;
		for (size_t i = 0; i < push_n; i++)
		{
			VectorEnlargeCapacity(vec);
			memcpy((char*)vec->at(vec, sizen), (char*)p1 + i * vec->_type, vec->_type);
			sizen++;
			vec->_current++;
		}
		memcpy((char*)vec->at(vec, sizen), ptr, size);
		free(ptr);
	}
}