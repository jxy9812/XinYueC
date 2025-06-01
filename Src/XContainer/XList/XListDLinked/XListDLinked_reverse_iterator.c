#include"XListDLinked_reverse_iterator.h"
#if XListDLinked_ON
#include"XListDLinked.h"
#include"stdio.h"
#include"XListDNode.h"
XListDLinked_reverse_iterator* XListDLinked_rbegin(XListDLinked* this_list)
{
	if (ISNULL(this_list, "XListDLinked_rbegin"))
		return NULL;
	return XContainerData(this_list,XListDNode).prev;
}

XListDLinked_reverse_iterator* XListDLinked_rend(XListDLinked* this_list)
{
	return NULL;
}

XListDLinked_reverse_iterator* XListDLinked_reverse_iterator_add(XListDLinked* this_list, XListDLinked_reverse_iterator* it)
{
	if (ISNULL(this_list, "XListDLinked_iterator_add  struct XListDLinked*"))
		return NULL;
	if (ISNULL(it, "XListDLinked_iterator_add  Xstruct XListDLinked_iterator*"))
		return NULL;
	XListDLinked_reverse_iterator* front = XListDLinked_begin(this_list);
	if (it == front)//如果是第一个元素则返回空表示遍历完成了
		return NULL;
	XListDNode* nodes = (XListDNode*)it;
	return nodes->prev;//指向上一个元素
}

void XListDLinked_reverse_iterator_for_each(XListDLinked* this_list, XFor_each ForFunction, void* args)
{
	for (XListDLinked_reverse_iterator* it = XListDLinked_rbegin(this_list); it != XListDLinked_rend(this_list); it = XListDLinked_reverse_iterator_add(this_list, it))
	{
		ForFunction(((XListDNode*)it)->data, args);
	}
}



#endif