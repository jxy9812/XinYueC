#include"XListSLinked_iterator.h"
#if XListSLinked_ON
#include"XListSLinked.h"
#include<stdio.h>

XListSLinked_iterator XListSLinked_begin(XListSLinked* this_list)
{
	XListSLinked_iterator it = { 0 };
	if (ISNULL(this_list, "XListSLinked_begin"))
		return it;
	it.node = XContainerDataPtr(this_list);
	return it;
}

XListSLinked_iterator XListSLinked_end(XListSLinked* this_list)
{
	XListSLinked_iterator it = { 0 };
	return it;
}

void XListSLinked_iterator_add(XListSLinked* this_list,XListSLinked_iterator*it)
{
	if (ISNULL(this_list, "XListSLinked_iterator_add  struct XListSLinked*"))
		return ;
	if (ISNULL(it, "XListSLinked_iterator_add  XStruct XListSLinked_iterator*"))
		return ;
	//XListSLinked_iterator*  back= this_list->m_tail;
	//if(it->node== back)//如果是最后一个元素则返回空表示遍历完成了
	//{
	//	it->node = NULL;
	//	return;
	//}
	it->node = (XListSNode*)(it->node)->next;//指向下一个元素
}

bool XListSLinked_iterator_equality(XListSLinked_iterator* itFirst, XListSLinked_iterator* itSecond)
{
	return itFirst->node == itSecond->node;
}

void XListSLinked_iterator_for_each(XListSLinked* this_list, XFor_each ForFunction, void* args)
{
	For_Each_Iterator(this_list, XListSLinked, it)
	{
		ForFunction(XListSLinked_iterator_data(&it), args);
	}
}

void* XListSLinked_iterator_data(XListSLinked_iterator* it)
{
	if (it == NULL || it->node == NULL)
		return NULL;
	return XListSNode_DataPtr(it->node);
}


#endif