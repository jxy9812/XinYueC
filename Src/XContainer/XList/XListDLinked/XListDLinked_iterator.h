#include"XDataStructConfig.h"
#if !defined(XLISTDLINKED_ITERATOR_H)&& XListDLinked_ON
#define XLISTDLINKED_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XFunctionCallback.h"
XContainerTypeDeclare(XListDLinked);
XContainerTypeDeclare(XListDNode);
//正向迭代器
typedef XListDNode XListDLinked_iterator;
XListDLinked_iterator* XListDLinked_begin(XListDLinked* this_list);
XListDLinked_iterator* XListDLinked_end(XListDLinked* this_list);
XListDLinked_iterator* XListDLinked_iterator_add(XListDLinked* this_list,XListDLinked_iterator*it);
void XListDLinked_iterator_for_each(XListDLinked* this_list, XFor_each ForFunction, void* args);
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
