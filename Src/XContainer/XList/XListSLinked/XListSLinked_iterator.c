#include"XListSLinked_iterator.h"
#if XListSLinked_ON
#include"XListSLinked.h"
#include<stdio.h>

XListSLinked_iterator* XListSLinked_begin(XListSLinked* this_list)
{
	if (ISNULL(this_list, "XListSLinked_begin"))
		return NULL;
	return XContainerDataPtr(this_list);
}

XListSLinked_iterator* XListSLinked_end(XListSLinked* this_list)
{
	return NULL;
}

XListSLinked_iterator* XListSLinked_iterator_add(XListSLinked* this_list,XListSLinked_iterator*it)
{
	if (ISNULL(this_list, "XListSLinked_iterator_add  struct XListSLinked*"))
		return NULL;
	if (ISNULL(it, "XListSLinked_iterator_add  XStruct XListSLinked_iterator*"))
		return NULL;
	XListSLinked_iterator*  back= this_list->m_tail;
	if(it== back)//如果是最后一个元素则返回空表示遍历完成了
		return NULL;
	return ((XListSNode*)it)->next;//指向下一个元素
}

void XListSLinked_iterator_for_each(XListSLinked* this_list, XFor_each ForFunction, void* args)
{
	//for (XListSLinked_iterator* it = XListSLinked_begin(this_list); it != XListSLinked_end(this_list); it = XListSLinked_iterator_add(this_list, it))
	for_each_iterator(this_list, XListSLinked,it)
	{
		ForFunction(XListSNode_DataPtr(it), args);
	}
}


#endif