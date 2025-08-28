#include "XDataStructConfig.h"
#if !defined(XHASHSET_ITERATOR_H) && XHashSet_ON
#define XHASHSET_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XSetBase_iterator.h"
XContainerTypeDeclare(XHashSet);
//typedef  XSetBase_iterator  XHashSet_iterator;
typedef struct XHashSet_iterator
{
    union //访问方便实际两个变量是一个
    {
        void* node;   // 当前节点
        XSetBase_iterator parent;
    };
    size_t index; // 当前index
} XHashSet_iterator;

XHashSet_iterator XHashSet_begin(XHashSet* this_set);
XHashSet_iterator XHashSet_end(XHashSet* this_set);
bool XHashSet_iterator_isEnd(const XHashSet_iterator* it);
void XHashSet_iterator_add(XHashSet* this_set, XHashSet_iterator* it);
bool XHashSet_iterator_equality(XHashSet_iterator* itFirst, XHashSet_iterator* itSecond);
void XHashSet_iterator_for_each(XHashSet* this_set, XFor_each ForFunction, void* args);
void* XHashSet_iterator_data(XHashSet_iterator* it);
#ifdef __cplusplus
}
#endif
#endif