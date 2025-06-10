#include"XHashMap_Iterator.h"
#if XHashMap_ON
#include"XHashMap.h"
XHashMap_iterator XHashMap_begin(XHashMap* this_map)
{
	XHashMap_iterator it = {0};
	if (this_map == NULL|| XContainerCapacity(this_map)==0)
		return it;
	XHashMapNode* node = NULL;
	for (size_t i = 0; i < XContainerCapacity(this_map); i++)
	{
		node = ((XHashMapNode**)XContainerDataPtr(this_map))[i];
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

XHashMap_iterator XHashMap_end(XHashMap* this_map)
{
	XHashMap_iterator it = { 0 };
	if (this_map == NULL || XContainerCapacity(this_map) == 0)
		return it;
	it.index = XContainerCapacity(this_map);
	return it;
}

void XHashMap_iterator_add(XHashMap* this_map, XHashMap_iterator* curent)
{
	//XHashMap_iterator it = { 0 };
	if (this_map == NULL || curent==NULL|| XContainerCapacity(this_map) == 0)
		return ;
	XHashMapNode* node = curent->node;
	if (node->next!=NULL)
	{
		curent->node = node->next;
		return;//下一个节点找到了
	}
	for (size_t i = curent->index+1; i < XContainerCapacity(this_map); i++)
	{
		node = ((XHashMapNode**)XContainerDataPtr(this_map))[i];
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

bool XHashMap_iterator_equality(XHashMap_iterator* itFirst, XHashMap_iterator* itSecond)
{
	return (itFirst->index == itSecond->index)&&(itFirst->node==itSecond->node);
}

void XHashMap_iterator_for_each(XHashMap* this_map, XFor_each ForFunction, void* args)
{
	for (XHashMap_iterator it = XHashMap_begin(this_map), endIt = XHashMap_end(this_map); !XHashMap_iterator_equality(&it, &endIt); XHashMap_iterator_add(this_map, &it))
	{
		ForFunction(XHashMap_data(&it),args);
	}
}

XPair* XHashMap_data(XHashMap_iterator* it)
{
	return ((XHashMapNode*)(it->node))->pair;
}

#endif
