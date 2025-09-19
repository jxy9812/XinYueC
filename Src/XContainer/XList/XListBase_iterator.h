#include"CXinYueConfig.h"
#if !defined(XLISTBASE_ITERATOR_ITERATOR_H)
#define XLISTBASE_ITERATOR_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
XContainerTypeDeclare(XListBase);
typedef struct XListBase_iterator
{
    void* node;   // 当前节点
} XListBase_iterator;

//XListBase_iterator XListBase_begin(XListBase* this_set);
//XListBase_iterator XListBase_end(XListBase* this_set);
//bool XListBase_iterator_isEnd(const XListBase_iterator* it);
//void XListBase_iterator_add(XListBase* this_set, XListBase_iterator* it);
//bool XListBase_iterator_equality(XListBase_iterator* itFirst, XListBase_iterator* itSecond);
//void XListBase_iterator_for_each(XListBase* this_set, XFor_each ForFunction, void* args);
//void* XListBase_iterator_data(XListBase_iterator* it);
#ifdef __cplusplus
}
#endif
#endif