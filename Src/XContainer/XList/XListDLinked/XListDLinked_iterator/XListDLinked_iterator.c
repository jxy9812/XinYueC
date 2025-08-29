#include"XListDLinked_iterator.h"
#if XListDLinked_ON
#include"XListDLinked.h"
#include<stdio.h>

XListDLinked_iterator XListDLinked_begin(XListDLinked* this_list)
{
	XListDLinked_iterator it = { 0 };
	if (ISNULL(this_list, "XListDLinked_begin"))
		return it;
	it.node= XContainerDataPtr(this_list);
	return it;
}

XListDLinked_iterator XListDLinked_end(XListDLinked* this_list)
{
	XListDLinked_iterator it = { 0 };
	return it;
}

bool XListDLinked_iterator_isEnd(const XListDLinked_iterator* it)
{
	return it ? (it->node == NULL) : false;
}

void XListDLinked_iterator_add(XListDLinked* this_list,XListDLinked_iterator*it)
{
	if (ISNULL(this_list, "XListDLinked_iterator_add  struct XListDLinked*"))
		return ;
	if (ISNULL(it, "XListDLinked_iterator_add  XStruct XListDLinked_iterator*"))
		return ;
	XListDLinked_iterator*  back= XContainerData(this_list, XListDNode*)->prev;
	if (it->node == back)//如果是最后一个元素则返回空表示遍历完成了
	{
		it->node = NULL;
		return;
	}
	it->node = ((XListDNode*)(it->node))->next;//指向下一个元素
}

bool XListDLinked_iterator_equality(XListDLinked_iterator* itFirst, XListDLinked_iterator* itSecond)
{
	return itFirst->node==itSecond->node;
}


void XListDLinked_iterator_for_each(XListDLinked* this_list, XFor_each ForFunction, void* args)
{
	for_each_iterator(this_list, XListDLinked,it)
	{
		ForFunction(XListDLinked_iterator_data(&it), args);
	}
}

void* XListDLinked_iterator_data(XListDLinked_iterator* it)
{
	if (it == NULL || it->node == NULL)
		return NULL;
	return XListDNode_DataPtr(it->node);
}


#endif