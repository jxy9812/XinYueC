#include "XVector_Iterator/XVector_reverse_iterator.h"
#include"XVector.h"
#include"XVector_head.h"
#include"stdio.h"
struct XVector_reverse_iterator* XVector_rbegin(struct XVector* this_vector)
{
	if (isObjectNULL(this_vector, "XVector_rbegin"))
		return NULL;
	return XVector_back(this_vector);
}

struct XVector_reverse_iterator* XVector_rend(struct XVector* this_vector)
{
	return NULL;
}

struct XVector_reverse_iterator* XVector_reverse_iterator_add(struct XVector* this_vector, struct XVector_reverse_iterator* it)
{
	if (isObjectNULL(this_vector, "XVector_iterator_add  struct XVector*"))
		return NULL;
	if (isObjectNULL(it, "XVector_iterator_add  XVector_iterator*"))
		return NULL;
	XVector_reverse_iterator* back = XVector_front(this_vector);
	if (it == back)//如果是第一个元素则返回空表示遍历完成了
		return NULL;
	XVECTOR* v = (XVECTOR*)this_vector;
	return (char*)it - v->object._type;//指向上一个元素
}
