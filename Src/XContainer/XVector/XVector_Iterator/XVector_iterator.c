#include "XVector_iterator.h"
#include"XVector.h"
#include<stdio.h>
XVector_iterator* XVector_begin(struct XVector* this_vector)
{
	if (isNULL(isNULLInfo(this_vector, "")))
		return NULL;
	return XVector_front(this_vector);
}

XVector_iterator* XVector_end(struct XVector* this_vector)
{
	return NULL;
}

XVector_iterator* XVector_iterator_add(struct XVector* this_vector,XVector_iterator*it)
{
	if (isNULL(isNULLInfo(this_vector, "")))
		return NULL;
	if (isNULL(isNULLInfo(it, "")))
		return NULL;
	XVector_iterator*  back= XVector_back(this_vector);
	if(it== back)//如果是最后一个元素则返回空表示遍历完成了
	return NULL;
	return (char*)it + this_vector->object._typeSize;//指向下一个元素
}
