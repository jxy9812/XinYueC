#include"vector_head.h"
#include<stdlib.h>
#include<string.h>

struct vector;
void Vector_pop_back(struct vector* this_vector)//删除向量中最后一个元素
{
	VECTOR* v=(VECTOR*)this_vector;
	v->object._size--;
}
void Vector_erase_p(struct vector* this_vector, const void* p1, const void* p2)//删除指针区间内的数据
{
	VECTOR* v=(VECTOR*)this_vector;
	if (p1 <= p2 && v->front(v) <= p1 && p2 <= v->back(v))
	{
		memcpy(p1, (char*)p2 + v->object._type, (int)((char*)v->back(v) - (char*)p2));
		v->object._size -= (((int)((char*)p2 - (char*)p1)) / v->object._type + 1);
	}
}
void Vector_erase_int(struct vector* this_vector, const int left, const int right)//删除区间内的数据
{
	VECTOR* v=(VECTOR*)this_vector;
	if (left <= right && left >= 0 && right < v->object._size)
	{
		memcpy(v->at(v, left), v->at(v, right + 1), (v->object._size - 1 - right) * v->object._type);
		v->object._size -= (right - left + 1);
	}
}