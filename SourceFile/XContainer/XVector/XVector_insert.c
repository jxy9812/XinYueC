#include"XVector_head.h"
#include<stdlib.h>
#include<string.h>
struct XVector;
void XVector_Push_Back(struct XVector* this_vector, void* val)
{
	if (isObjectNULL(this_vector, "XVector_Push_Back"))
		return;
	XVECTOR* v=(XVECTOR*)this_vector;
	VectorEnlargeCapacity(v);
	char* str1 = (char*)v->object._data + v->object._type * v->object._size;
	memcpy(str1, val, v->object._type);
	v->object._size++;
}
void XVector_insert_front(struct XVector* this_vector, const void* pSel, const void* val)
{
	if (isObjectNULL(this_vector, "XVector_insert_front"))
		return;
	XVECTOR* v=(XVECTOR*)this_vector;
	VectorEnlargeCapacity(v);
	if (pSel >= v->front(v) && pSel <= v->back(v))
	{
		int size = (char*)v->back(v)-(char*)pSel + v->object._type;
		void* ptr = malloc(size);
		memcpy(ptr, pSel, size);
		memcpy(pSel, val, v->object._type);
		memcpy((char*)pSel + v->object._type, ptr, size);
		v->object._size++;
		free(ptr);
	}
}
void XVector_insert_nfront(struct XVector* this_vector, const void* pSel, const int n, const void* val)// 向量中指向元素p前增加n个相同的元素x
{
	if (isObjectNULL(this_vector, "XVector_insert_nfront"))
		return;
	XVECTOR* v=(XVECTOR*)this_vector;
	if (pSel >= v->front(v) && pSel <= v->back(v))
	{
		VectorEnlargeCapacity(v);
		int size = (char*)v->back(v) -(char*)pSel + v->object._type;
		void* ptr = malloc(size);
		memcpy(ptr, pSel, size);
		int sizen = ((char*)pSel - (char*)v->front(v)) / v->object._type;
		for (size_t i = 0; i < n; i++)
		{
			VectorEnlargeCapacity(v);
			memcpy((char*)v->at(v, sizen), val, v->object._type);
			sizen++;
			v->object._size++;
		}
		memcpy((char*)v->at(v, sizen), ptr, size);
		free(ptr);
	}
}
void XVector_insert(struct XVector* this_vector, const void* pSel, const void* p1, const void* p2)// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2)间的数据
{
	if (isObjectNULL(this_vector, "XVector_insert"))
		return;
	XVECTOR* v=(XVECTOR*)this_vector;
	if (pSel >= v->front(v) && pSel <= v->back(v))
	{
		VectorEnlargeCapacity(v);
		int size = (char*)v->back(v) -(char*)pSel + v->object._type;
		void* ptr = malloc(size);
		memcpy(ptr, pSel, size);
		int sizen = ((char*)pSel - (char*)v->front(v)) / v->object._type;
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