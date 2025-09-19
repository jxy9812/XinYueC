#include"CXinYueConfig.h"
#if !defined(XSET_ITERATOR_H)&& XSet_ON
#define XSET_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XSetBase_iterator.h"
XContainerTypeDeclare(XSet);
typedef  XSetBase_iterator  XSet_iterator;

XSet_iterator XSet_begin(XSet* this_map);
XSet_iterator XSet_end(XSet* this_map);
bool XSet_iterator_isEnd(const XSet_iterator* it);
void XSet_iterator_add(XSet* this_map, XSet_iterator* it);
bool XSet_iterator_equality(XSet_iterator* itFirst, XSet_iterator* itSecond);
void XSet_iterator_for_each(XSet* this_map, XFor_each ForFunction, void* args);
void* XSet_iterator_data(XSet_iterator* it);
#ifdef __cplusplus
}
#endif
#endif