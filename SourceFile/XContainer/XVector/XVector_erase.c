#include"XVector_head.h"
#include"XVector_func.h"
#include<stdlib.h>
#include<string.h>

struct XVector;
void XVector_pop_back(struct XVector* this_vector)//删除向量中最后一个元素
{
	if (isObjectNULL(this_vector, "XVector_pop_back"))
		return;
	XVECTOR* v=(XVECTOR*)this_vector;
	v->object._size--;
}
void XVector_erase_p(struct XVector* this_vector, const void* p1, const void* p2)//删除指针区间内的数据
{
	if (isObjectNULL(this_vector, "XVector_erase_p"))
		return;
	XVECTOR* v=(XVECTOR*)this_vector;
	if (p1 <= p2 && v->front(v) <= p1 && p2 <= v->back(v))
	{
		memcpy(p1, (char*)p2 + v->object._type, (int)((char*)v->back(v) - (char*)p2));
		v->object._size -= (((int)((char*)p2 - (char*)p1)) / v->object._type + 1);
	}
}
void XVector_erase_int(struct XVector* this_vector, const int left, const int right)//删除区间内的数据
{
	if (isObjectNULL(this_vector, "XVector_erase_int"))
		return;
	XVECTOR* v=(XVECTOR*)this_vector;
	if (left <= right && left >= 0 && right < v->object._size)
	{
		if (XVector_size(this_vector) == 0)
		{
			return;
		}
		else if (XVector_size(this_vector) == 1)
		{
			v->object._size =0;
		}
		else
		{
			memcpy(v->at(v, left), v->at(v, right + 1), (v->object._size - 1 - right) * v->object._type);
			v->object._size -= (right - left + 1);
		}
	}
	
}