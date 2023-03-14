#include "XList_reverse_iterator.h"
#include"XList.h"
//#include"XList_head.h"
#include"stdio.h"
#include"XListNode.h"
XList_reverse_iterator* XList_rbegin(XList* this_list)
{
	if (isNULL(this_list, "XList_rbegin"))
		return NULL;
	return XList_back(this_list);
}

XList_reverse_iterator* XList_rend(XList* this_list)
{
	return NULL;
}

XList_reverse_iterator* XList_reverse_iterator_add(XList* this_list, XList_reverse_iterator* it)
{
	if (isNULL(this_list, "XList_iterator_add  struct XList*"))
		return NULL;
	if (isNULL(it, "XList_iterator_add  Xstruct XList_iterator*"))
		return NULL;
	XList_reverse_iterator* front = XList_front(this_list);
	if (it == front)//如果是第一个元素则返回空表示遍历完成了
		return NULL;
	XListNode* nodes = (XListNode*)it;
	return nodes->prev;//指向上一个元素
}
