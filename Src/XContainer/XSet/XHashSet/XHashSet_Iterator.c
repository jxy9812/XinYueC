#include "XHashSet_Iterator.h"
#if XHashSet_ON
#include "XHashSet.h"

XHashSet_iterator XHashSet_begin(XHashSet* this_set)
{
    XHashSet_iterator it = { 0 };
    if (this_set == NULL || XContainerCapacity(this_set) == 0)
        return it;
    XHashSetNode* node = NULL;
    for (size_t i = 0; i < XContainerCapacity(this_set); i++)
    {
        node = ((XHashSetNode**)XContainerDataPtr(this_set))[i];
        if (node != NULL)
        {
            it.node = node;
            it.index = i;
            return it; // 下一个节点找到了
        }
    }
    it.node = NULL;
    it.index = XContainerCapacity(this_set);
    return it;
}

XHashSet_iterator XHashSet_end(XHashSet* this_set)
{
    XHashSet_iterator it = { 0 };
    if (this_set == NULL || XContainerCapacity(this_set) == 0)
        return it;
    it.index = XContainerCapacity(this_set);
    return it;
}

void XHashSet_iterator_add(XHashSet* this_set, XHashSet_iterator* curent)
{
    if (this_set == NULL || curent == NULL || XContainerCapacity(this_set) == 0)
        return;
    XHashSetNode* node = curent->node;
    if (node->next != NULL)
    {
        curent->node = node->next;
        return; // 下一个节点找到了
    }
    for (size_t i = curent->index + 1; i < XContainerCapacity(this_set); i++)
    {
        node = ((XHashSetNode**)XContainerDataPtr(this_set))[i];
        if (node != NULL)
        {
            curent->node = node;
            curent->index = i;
            return; // 下一个节点找到了
        }
    }
    curent->node = NULL;
    curent->index = XContainerCapacity(this_set);
}

bool XHashSet_iterator_equality(XHashSet_iterator* itFirst, XHashSet_iterator* itSecond)
{
    return (itFirst->index == itSecond->index) && (itFirst->node == itSecond->node);
}

void XHashSet_iterator_for_each(XHashSet* this_set, XFor_each ForFunction, void* args)
{
    for_each_iterator(this_set, XHashSet, it)
    {
        ForFunction(XHashSet_iterator_data(&it), args);
    }
}

void* XHashSet_iterator_data(XHashSet_iterator* it)
{
    return ((XHashSetNode*)(it->node))->key;
}

#endif