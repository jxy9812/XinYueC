#include "XString_iterator.h"
#if  XString_ON
#include "XString.h"
XString_iterator XString_begin(XString* str)
{
	if (XString_isEmpty_base(str))
		return XString_end(str);
	XString_iterator it = { 0 };
	if (ISNULL(str, ""))
		return it;
	it.data = XContainerDataPtr(str);
	return it;
}

XString_iterator XString_end(XString* str)
{
	XString_iterator it = { 0 };
	return it;
}
bool XString_iterator_isEnd(const XString_iterator* it)
{
	return it ? (it->data == NULL) : false;
}
void XString_iterator_add(XString* str, XString_iterator* it)
{
	if (ISNULL(str, "") || ISNULL(it, ""))
		return;
	if(XString_isEmpty_base(str))
	{
		*it=XString_end(str);
		return;
	}
	XChar* back = ((XChar*)XContainerDataPtr(str)) + (XString_length_base(str)-1);
	if (it->data == back)//如果是最后一个元素则返回空表示遍历完成了
	{
		it->data = NULL;
		return;
	}
	((XChar*)it->data) += 1;//指向下一个元素
}
bool XString_iterator_equality(XString_iterator* itFirst, XString_iterator* itSecond)
{
	return itFirst->data == itSecond->data;
}
void XString_iterator_for_each(XString* str, XFor_each ForFunction, void* args)
{
	if (str == NULL || ForFunction == NULL)
		return;
	for_each_iterator(str, XString, it)
	{
		ForFunction(XString_iterator_data(&it), args);
	}
}
XChar* XString_iterator_data(XString_iterator* it)
{
	if (it == NULL || it->data == NULL)
		return NULL;
	return it->data;
}
#endif