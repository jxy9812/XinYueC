#include "XList_iterator.h"
#include"XList.h"
//#include"XList_head.h"
#include<stdio.h>
#include"XListNode.h"

XList_iterator* XList_begin(struct XList* this_list)
{
	if (ISNULL(this_list, "XList_begin"))
		return NULL;
	return XList_front(this_list);
}

XList_iterator* XList_end(struct XList* this_list)
{
	return NULL;
}

XList_iterator* XList_iterator_add(struct XList* this_list,XList_iterator*it)
{
	if (ISNULL(this_list, "XList_iterator_add  struct XList*"))
		return NULL;
	if (ISNULL(it, "XList_iterator_add  Xstruct XList_iterator*"))
		return NULL;
	XList_iterator*  back= XList_back(this_list);
	if(it== back)//如果是最后一个元素则返回空表示遍历完成了
	return NULL;
	XListNode* nodes = (XListNode*)it;
	return nodes->next;//指向下一个元素
}
