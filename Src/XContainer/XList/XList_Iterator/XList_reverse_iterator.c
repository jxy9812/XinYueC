#include"XList_reverse_iterator.h"
#if XList_ON
#include"XList.h"
#include"stdio.h"
#include"XListNode.h"
XList_reverse_iterator* XList_rbegin(XList* this_list)
{
	if (ISNULL(this_list, "XList_rbegin"))
		return NULL;
	return XContainerData(this_list,XListNode).prev;
}

XList_reverse_iterator* XList_rend(XList* this_list)
{
	return NULL;
}

XList_reverse_iterator* XList_reverse_iterator_add(XList* this_list, XList_reverse_iterator* it)
{
	if (ISNULL(this_list, "XList_iterator_add  struct XList*"))
		return NULL;
	if (ISNULL(it, "XList_iterator_add  Xstruct XList_iterator*"))
		return NULL;
	XList_reverse_iterator* front = XList_begin(this_list);
	if (it == front)//如果是第一个元素则返回空表示遍历完成了
		return NULL;
	XListNode* nodes = (XListNode*)it;
	return nodes->prev;//指向上一个元素
}

void XList_reverse_iterator_for_each(XList* this_list, XFor_each ForFunction, void* args)
{
	for (XList_reverse_iterator* it = XList_rbegin(this_list); it != XList_rend(this_list); it = XList_reverse_iterator_add(this_list, it))
	{
		ForFunction(((XListNode*)it)->date, args);
	}
}



#endif