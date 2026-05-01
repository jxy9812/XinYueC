#include"CXinYueConfig.h"
#if !defined(XSETBASE_ITERATOR_H)
#define XSETBASE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainer_iterator.h"
#include"XFunctionCallback.h"
XContainerTypeDeclare(XSetBase);
typedef struct XSetBase_iterator
{
    //size_t index; // 当前index
    void* node;   // 当前节点
} XSetBase_iterator;

//XSetBase_iterator XSetBase_begin(XSetBase* this_set);
//XSetBase_iterator XSetBase_end(XSetBase* this_set);
//bool XSetBase_iterator_isEnd(const XSetBase_iterator* it);
//void XSetBase_iterator_add(XSet* this_set, XSetBase_iterator* it);
//bool XSetBase_iterator_equality(XSetBase_iterator* itFirst, XSetBase_iterator* itSecond);
//void XSetBase_iterator_for_each(XSet* this_set, XFor_each ForFunction, void* argList);
//void* XSetBase_iterator_data(XSetBase_iterator* it);
#ifdef __cplusplus
}
#endif
#endif