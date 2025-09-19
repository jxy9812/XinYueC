#include"CXinYueConfig.h"
#if !defined(XLISTSLINKED_ITERATOR_H)&& XListSLinked_ON
#define XLISTSLINKED_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XListBase_iterator.h"
XContainerTypeDeclare(XListSLinked);
XContainerTypeDeclare(XListSNode);
//正向迭代器
typedef XListBase_iterator XListSLinked_iterator;

XListSLinked_iterator XListSLinked_begin(XListSLinked* this_list);
XListSLinked_iterator XListSLinked_end(XListSLinked* this_list);
bool XListSLinked_iterator_isEnd(const XListSLinked_iterator* it);
void XListSLinked_iterator_add(XListSLinked* this_list,XListSLinked_iterator*it);
bool XListSLinked_iterator_equality(XListSLinked_iterator* itFirst, XListSLinked_iterator* itSecond);
void XListSLinked_iterator_for_each(XListSLinked* this_list, XFor_each ForFunction, void* args);
void* XListSLinked_iterator_data(XListSLinked_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
