#include"XDataStructConfig.h"
#if !defined(XLISTSLINKED_ITERATOR_H)&& XListSLinked_ON
#define XLISTSLINKED_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
XContainerTypeDeclare(XListSLinked);
XContainerTypeDeclare(XListSNode);
//正向迭代器
typedef XListSNode XListSLinked_iterator;
XListSLinked_iterator* XListSLinked_begin(XListSLinked* this_list);
XListSLinked_iterator* XListSLinked_end(XListSLinked* this_list);
XListSLinked_iterator* XListSLinked_iterator_add(XListSLinked* this_list,XListSLinked_iterator*it);
void XListSLinked_iterator_for_each(XListSLinked* this_list, XFor_each ForFunction, void* args);
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
