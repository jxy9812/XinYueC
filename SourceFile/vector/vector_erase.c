#include"vector.h"
#include"vector_head.h"
#include<stdlib.h>
#include<string.h>


void Vector_pop_back(struct vector* vec)//删除向量中最后一个元素
{
	VECTOR* vector=(VECTOR*)vec;
	vector->_current--;
}
void Vector_erase_p(struct vector* vec, const void* p1, const void* p2)//删除指针区间内的数据
{
	VECTOR* vector=(VECTOR*)vec;
	if (p1 <= p2 && vector->front(vec) <= p1 && p2 <= vector->back(vec))
	{
		memcpy(p1, (char*)p2 + vector->_type, (int)((char*)vector->back(vec) - (char*)p2));
		vector->_current -= (((int)((char*)p2 - (char*)p1)) / vector->_type + 1);
	}
}
void Vector_erase_int(struct vector* vec, const int left, const int right)//删除区间内的数据
{
	VECTOR* vector=(VECTOR*)vec;
	if (left <= right && left >= 0 && right < vector->_current)
	{
		memcpy(vector->at(vec, left), vector->at(vec, right + 1), (vector->_current - 1 - right) * vector->_type);
		vector->_current -= (right - left + 1);
	}
}