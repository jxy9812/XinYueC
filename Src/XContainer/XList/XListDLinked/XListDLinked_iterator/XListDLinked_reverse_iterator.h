#include"CXinYueConfig.h"
#if !defined(XLIST_REVERSE_ITERATOR_H)&& XListDLinked_ON
#define XLIST_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XListBase_iterator.h"
XContainerTypeDeclare(XListDLinked);
XContainerTypeDeclare(XListDNode);
//反向迭代器
typedef XListBase_iterator XListDLinked_reverse_iterator;

XListDLinked_reverse_iterator XListDLinked_rbegin(XListDLinked* this_list);
XListDLinked_reverse_iterator XListDLinked_rend(XListDLinked* this_list);
bool XListDLinked_reverse_iterator_isEnd(const XListDLinked_reverse_iterator* it);
void XListDLinked_reverse_iterator_add(XListDLinked* this_list, XListDLinked_reverse_iterator* it);
bool XListDLinked_reverse_iterator_equality(XListDLinked_reverse_iterator* itFirst, XListDLinked_reverse_iterator* itSecond);
void XListDLinked_reverse_iterator_for_each(XListDLinked* this_list, XFor_each ForFunction, void* args);
void* XListDLinked_reverse_iterator_data(XListDLinked_reverse_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H