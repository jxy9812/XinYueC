#include"CXinYueConfig.h"
#if !defined(XLockFreeList_ITERATOR_H) && XLockFreeList_ON
#define XLockFreeList_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XListBase_iterator.h"
XContainerTypeDeclare(XLockFreeList);
XContainerTypeDeclare(XLockFreeListNode);
// 正向迭代器
typedef XListBase_iterator XLockFreeList_iterator;

XLockFreeList_iterator XLockFreeList_begin(XLockFreeList* this_list);
XLockFreeList_iterator XLockFreeList_end(XLockFreeList* this_list);
bool XLockFreeList_iterator_isEnd(const XLockFreeList_iterator* it);
void XLockFreeList_iterator_add(XLockFreeList* this_list, XLockFreeList_iterator* it);
bool XLockFreeList_iterator_equality(XLockFreeList_iterator* itFirst, XLockFreeList_iterator* itSecond);
void XLockFreeList_iterator_for_each(XLockFreeList* this_list, XFor_each ForFunction, void* args);
void* XLockFreeList_iterator_data(XLockFreeList_iterator* it);

#ifdef __cplusplus
}
#endif
#endif // ! XLockFreeList_ITERATOR_H