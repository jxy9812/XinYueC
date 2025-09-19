#include"CXinYueConfig.h"
#if !defined(XMAP_ITERATOR_H)&& XMap_ON
#define XMAP_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XMapBase_iterator.h"
XContainerTypeDeclare(XMap);
typedef  XMapBase_iterator  XMap_iterator;

XMap_iterator XMap_begin(XMap* this_map);
XMap_iterator XMap_end(XMap* this_map);
bool XMap_iterator_isEnd(const XMap_iterator* it);
void XMap_iterator_add(XMap* this_map, XMap_iterator* it);
bool XMap_iterator_equality(XMap_iterator* itFirst, XMap_iterator* itSecond);
void XMap_iterator_for_each(XMap* this_map, XFor_each ForFunction, void* args);
XPair* XMap_iterator_data(XMap_iterator* it);
#ifdef __cplusplus
}
#endif
#endif