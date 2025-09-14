#include "XString_reverse_iterator.h"
#if  XString_ON
#include "XString.h"
XString_reverse_iterator XString_rbegin(XString* str)
{
	if (XString_isEmpty_base(str))
		return XString_rend(str);
	XChar* back = ((XChar*)XContainerDataPtr(str)) + (XString_length_base(str) - 1);
	return  (XString_reverse_iterator) { back };
}

XString_reverse_iterator XString_rend(XString* str)
{
	return  (XString_reverse_iterator) { 0 };
}

bool XString_reverse_iterator_isRend(const XString_reverse_iterator* it)
{
	return it ? (it->data == NULL) : false;
}

void XString_reverse_iterator_add(XString* str, XString_reverse_iterator* it)
{
	if (ISNULL(str, "") || ISNULL(it, ""))
		return;
	if (XString_isEmpty_base(str))
	{
		*it = XString_rend(str);
		return;
	}
	XChar* front = ((XChar*)XContainerDataPtr(str));
	if (it->data == front)//如果是第一个元素表示遍历完成了
	{
		it->data = NULL;
		return;
	}
	it->data = ((XChar*)(it->data)) - 1;//指向上一个元素
}

bool XString_reverse_iterator_equality(XString_reverse_iterator* itFirst, XString_reverse_iterator* itSecond)
{
	return itFirst->data == itSecond->data;
}

void XString_reverse_iterator_for_each(XString* str, XFor_each ForFunction, void* args)
{
	if (str == NULL || ForFunction == NULL)
		return;
	for_each_reverse_iterator(str, XString, it)
	{
		ForFunction(XString_reverse_iterator_data(&it), args);
	}
}

XChar* XString_reverse_iterator_data(XString_reverse_iterator* it)
{
	if (it == NULL || it->data == NULL)
		return NULL;
	return it->data;
}
#endif