#include"vector.h"
#include"vector_head.h"
#include<stdlib.h>
#include<string.h>


void Vector_pop_back(struct VECTOR* vec)//删除向量中最后一个元素
{
	vec->_current--;
}
void Vector_erase_p(struct VECTOR* vec, const void* p1, const void* p2)//删除指针区间内的数据
{
	if (p1 <= p2 && vec->front(vec) <= p1 && p2 <= vec->back(vec))
	{
		memcpy(p1, (char*)p2 + vec->_type, (int)((char*)vec->back(vec) - (char*)p2));
		vec->_current -= (((int)((char*)p2 - (char*)p1)) / vec->_type + 1);
	}
}
void Vector_erase_int(struct VECTOR* vec, const int left, const int right)//删除区间内的数据
{
	if (left <= right && left >= 0 && right < vec->_current)
	{
		memcpy(vec->at(vec, left), vec->at(vec, right + 1), (vec->_current - 1 - right) * vec->_type);
		vec->_current -= (right - left + 1);
	}
}