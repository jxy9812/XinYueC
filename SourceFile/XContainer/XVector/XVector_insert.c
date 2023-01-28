#include"XVector_head.h"
#include"XVector_func.h"
#include<stdlib.h>
#include<string.h>
struct XVector;
void XVector_push_front(struct XVector* this_vector, void* LValue)
{
	if (XVector_empty(this_vector))
		XVector_push_back(this_vector, LValue);
	else
		XVector_insert_front(this_vector, XVector_front(this_vector), LValue);
}
void XVector_push_back(struct XVector* this_vector, void* val)
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
	size_t TypeSize = XVector_TypeSize(this_vector);//数据类型的大小
	char* pfront = XVector_front(this_vector);//头指针
	char* pback = XVector_back(this_vector);//尾指针
	if (pSel >= pfront && pSel <= pback)
	{
		int size = pback -(char*)pSel + TypeSize;//后半段数据的大小
		size_t nSel = ((char*)pSel - (char*)XVector_front(this_vector)) / (TypeSize);//当前数据在数组中的索引号
		void* ptr = malloc(size);
		if (ptr == NULL)
			return;
		VectorEnlargeCapacity(v);
		char* UpData_pSel = XVector_at(this_vector,nSel);//扩容后需要重新定位指针
		//将后半段数据往后挪一下位置
		memcpy(ptr, UpData_pSel, size);
		memcpy(UpData_pSel + TypeSize, ptr, size);
		free(ptr);
		//插入新数据
		memcpy(UpData_pSel, val, TypeSize);
		v->object._size++;
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