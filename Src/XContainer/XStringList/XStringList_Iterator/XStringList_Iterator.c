#include "XStringList_Iterator.h"
#if XStringList_ON
#include"XString.h"
#include<stdio.h>
XStringList_iterator XStringList_begin(XStringList* this_XStringList)
{
	return XVector_begin(this_XStringList);
}

XStringList_iterator XStringList_end(XStringList* this_XStringList)
{
	return XVector_end(this_XStringList);
}

void XStringList_iterator_add(XStringList* this_XStringList, XStringList_iterator* it)
{
	if (ISNULL(this_XStringList, "") || ISNULL(it, ""))
		return;
	void* back = XVtableGetFunc(XVector_class_init(), EXVector_Back, void* (*)(XVector*))(this_XStringList);
	if (it->data == back)//如果是最后一个元素则返回空表示遍历完成了
	{
		it->data = NULL;
		return;
	}
	it->data = ((char*)(it->data)) + ((XContainerObject*)this_XStringList)->m_typeSize;//指向下一个元素
}

bool XStringList_iterator_equality(XStringList_iterator* itFirst, XStringList_iterator* itSecond)
{
	return XVector_iterator_equality(itFirst,itSecond);
}

void XStringList_iterator_for_each(XStringList* this_XStringList, XFor_each ForFunction, void* args)
{
	if (this_XStringList == NULL || ForFunction == NULL)
		return;
	for_each_iterator(this_XStringList, XStringList, it)
	{
		/*printf("迭代器\n");*/
		ForFunction(XStringList_iterator_data(&it), args);
	}
}
XString* XStringList_iterator_data(XStringList_iterator* it)
{
	return *((XString**)XVector_iterator_data(it));
}
#endif


