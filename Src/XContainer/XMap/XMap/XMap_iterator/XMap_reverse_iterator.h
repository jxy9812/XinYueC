#include"XDataStructConfig.h"
#if !defined(XMAP_REVERSE_ITERATOR_H)&& XMap_ON
#define XMAP_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XMapBase_iterator.h"
XContainerTypeDeclare(XMap);
typedef  XMapBase_iterator  XMap_reverse_iterator;

XMap_reverse_iterator XMap_rbegin(XMap* this_map);
XMap_reverse_iterator XMap_rend(XMap* this_map);
bool XMap_reverse_iterator_isRend(const XMap_reverse_iterator* it);
void  XMap_reverse_iterator_add(XMap* this_map, XMap_reverse_iterator* it);
bool XMap_reverse_iterator_equality(XMap_reverse_iterator* itFirst, XMap_reverse_iterator* itSecond);
void XMap_reverse_iterator_for_each(XMap* this_map, XFor_each ForFunction, void* args);
XPair* XMap_reverse_iterator_data(XMap_reverse_iterator* it);
#ifdef __cplusplus
}
#endif
#endif // !XMAP_REVERSE_ITERATOR_H
