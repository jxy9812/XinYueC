#include"XListDLinked_reverse_iterator.h"
#if XListDLinked_ON
#include"XListDLinked.h"
#include"stdio.h"
XListDLinked_reverse_iterator XListDLinked_rbegin(XListDLinked* this_list)
{
	XListDLinked_reverse_iterator it = { 0 };
	if (ISNULL(this_list, "XListDLinked_rbegin"))
		return it;
	it.node = XContainerData(this_list, XListDNode*)->prev;
	return it;
}

XListDLinked_reverse_iterator XListDLinked_rend(XListDLinked* this_list)
{
	XListDLinked_reverse_iterator it = { 0 };
	return it;
}

bool XListDLinked_reverse_iterator_isEnd(const XListDLinked_reverse_iterator* it)
{
	return it ? (it->node == NULL) : false;
}

void XListDLinked_reverse_iterator_add(XListDLinked* this_list, XListDLinked_reverse_iterator* it)
{
	if (ISNULL(this_list, "XListDLinked_iterator_add  struct XListDLinked*"))
		return ;
	if (ISNULL(it, "XListDLinked_iterator_add  Xstruct XListDLinked_iterator*"))
		return ;
	XListDLinked_reverse_iterator* front = XContainerDataPtr(this_list);
	if (it->node == front)//如果是第一个元素则返回空表示遍历完成了
	{
		it->node = NULL;
		return;
	}
	it->node = ((XListDNode*)(it->node))->prev;//指向下一个元素
}

bool XListDLinked_reverse_iterator_equality(XListDLinked_reverse_iterator* itFirst, XListDLinked_reverse_iterator* itSecond)
{
	return itFirst->node == itSecond->node;
}

void XListDLinked_reverse_iterator_for_each(XListDLinked* this_list, XFor_each ForFunction, void* args)
{
	for_each_reverse_iterator(this_list, XListDLinked, it)
	{
		ForFunction(XListDLinked_iterator_data(&it), args);
	}
}

void* XListDLinked_reverse_iterator_data(XListDLinked_reverse_iterator* it)
{
	if (it == NULL || it->node == NULL)
		return NULL;
	return XListDNode_DataPtr(it->node);
}



#endif