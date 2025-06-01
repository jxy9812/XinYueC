#include"XListDLinked_iterator.h"
#if XListDLinked_ON
#include"XListDLinked.h"
#include<stdio.h>
#include"XListDNode.h"

XListDLinked_iterator* XListDLinked_begin(XListDLinked* this_list)
{
	if (ISNULL(this_list, "XListDLinked_begin"))
		return NULL;
	return XContainerDataPtr(this_list);
}

XListDLinked_iterator* XListDLinked_end(XListDLinked* this_list)
{
	return NULL;
}

XListDLinked_iterator* XListDLinked_iterator_add(XListDLinked* this_list,XListDLinked_iterator*it)
{
	if (ISNULL(this_list, "XListDLinked_iterator_add  struct XListDLinked*"))
		return NULL;
	if (ISNULL(it, "XListDLinked_iterator_add  Xstruct XListDLinked_iterator*"))
		return NULL;
	XListDLinked_iterator*  back= XListDLinked_rbegin(this_list);
	if(it== back)//如果是最后一个元素则返回空表示遍历完成了
	return NULL;
	XListDNode* nodes = (XListDNode*)it;
	return nodes->next;//指向下一个元素
}

void XListDLinked_iterator_for_each(XListDLinked* this_list, XFor_each ForFunction, void* args)
{
	for (XListDLinked_iterator* it = XListDLinked_begin(this_list); it != XListDLinked_end(this_list); it = XListDLinked_iterator_add(this_list, it))
	{
		ForFunction(((XListDNode*)it)->date, args);
	}
}


#endif