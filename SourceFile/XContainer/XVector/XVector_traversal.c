#include<stdlib.h>
#include<string.h>
#include"XVector_func.h"
struct XVector;
void* XVector_at(const struct XVector* this_vector, int nSel)// 返回元素的指针
{
	if (isNULL(isNULLInfo(this_vector, "")))
		return NULL;
	XContainerObject* object = XVector_object(this_vector);
	if (nSel + 1 > object->_size)
	{
		return NULL;
	}
	return (void*)((char*)object->_data +object->_type * nSel);
}
void* XVector_front(const struct XVector* this_vector)//返回向量头指针，指向第一个元素
{
	if (isNULL(isNULLInfo(this_vector, "")))
		return NULL;
	return XVector_object(this_vector)->_data;
}
void* XVector_back(const struct XVector* this_vector)//返回向量尾指针，指向向量最后一个元素
{
	if (isNULL(isNULLInfo(this_vector, "")))
		return NULL;
	XContainerObject* object = XVector_object(this_vector);
	char* _data = (char*)object->_data + object->_type * (object->_size - 1);
	return _data;
}
