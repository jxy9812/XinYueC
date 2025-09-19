#include"CXinYueConfig.h"
#if !defined(XMAPBASE_ITERATOR_ITERATOR_H)
#define XMAPBASE_ITERATOR_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XFunctionCallback.h"
XContainerTypeDeclare(XMapBase);
XContainerTypeDeclare(XPair);
typedef struct XMapBase_iterator
{
    //size_t index; // 当前index
    void* node;   // 当前节点
} XMapBase_iterator;

//XMapBase_iterator XMapBase_begin(XMapBase* this_set);
//XMapBase_iterator XMapBase_end(XMapBase* this_set);
//bool XMapBase_iterator_isEnd(const XMapBase_iterator* it);
//void XMapBase_iterator_add(XMapBase* this_set, XMapBase_iterator* it);
//bool XMapBase_iterator_equality(XMapBase_iterator* itFirst, XMapBase_iterator* itSecond);
//void XMapBase_iterator_for_each(XMapBase* this_set, XFor_each ForFunction, void* args);
//void* XMapBase_iterator_data(XMapBase_iterator* it);
#ifdef __cplusplus
}
#endif
#endif