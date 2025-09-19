#include"CXinYueConfig.h"
#if !defined(XLISTDLINKED_ITERATOR_H)&& XListDLinked_ON
#define XLISTDLINKED_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XListBase_iterator.h"
XContainerTypeDeclare(XListDLinked);
XContainerTypeDeclare(XListDNode);
//正向迭代器
typedef XListBase_iterator XListDLinked_iterator;

XListDLinked_iterator XListDLinked_begin(XListDLinked* this_list);
XListDLinked_iterator XListDLinked_end(XListDLinked* this_list);
bool XListDLinked_iterator_isEnd(const XListDLinked_iterator* it);
void XListDLinked_iterator_add(XListDLinked* this_list,XListDLinked_iterator*it);
bool XListDLinked_iterator_equality(XListDLinked_iterator* itFirst, XListDLinked_iterator* itSecond);
void XListDLinked_iterator_for_each(XListDLinked* this_list, XFor_each ForFunction, void* args);
void* XListDLinked_iterator_data(XListDLinked_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
