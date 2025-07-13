#include"XHash_Iterator.h"
#if XHash_ON
#include"XHash.h"
XHash_iterator XHash_begin(XHash*this_map)
{
	XHash_iterator it = {0};
	if (this_map == NULL|| XContainerCapacity(this_map)==0)
		return it;
	XHashNode* node = NULL;
	for (size_t i = 0; i < XContainerCapacity(this_map); i++)
	{
		node = ((XHashNode**)XContainerDataPtr(this_map))[i];
		if (node != NULL)
		{
			it.node = node;
			it.index = i;
			return it;//下一个节点找到了
		}
	}
	it.node = NULL;
	it.index = XContainerCapacity(this_map);
	return it;
}

XHash_iterator XHash_end(XHash*this_map)
{
	XHash_iterator it = { 0 };
	if (this_map == NULL || XContainerCapacity(this_map) == 0)
		return it;
	it.index = XContainerCapacity(this_map);
	return it;
}

void XHash_iterator_add(XHash*this_map, XHash_iterator* curent)
{
	//XHash_iterator it = { 0 };
	if (this_map == NULL || curent==NULL|| XContainerCapacity(this_map) == 0)
		return ;
	XHashNode* node = curent->node;
	if (node->next!=NULL)
	{
		curent->node = node->next;
		return;//下一个节点找到了
	}
	for (size_t i = curent->index+1; i < XContainerCapacity(this_map); i++)
	{
		node = ((XHashNode**)XContainerDataPtr(this_map))[i];
		if (node != NULL)
		{
			curent->node = node;
			curent->index = i;
			return;//下一个节点找到了
		}
	}
	curent->node = NULL;
	curent->index= XContainerCapacity(this_map);
}

bool XHash_iterator_equality(XHash_iterator* itFirst, XHash_iterator* itSecond)
{
	return (itFirst->index == itSecond->index)&&(itFirst->node==itSecond->node);
}

void XHash_iterator_for_each(XHash*this_map, XFor_each ForFunction, void* args)
{
	for_each_iterator(this_map, XHash, it)
	{
		ForFunction(XHash_iterator_data(&it),args);
	}
}

XPair* XHash_iterator_data(XHash_iterator* it)
{
	return ((XHashNode*)(it->node))->pair;
}

#endif
