#include"XDataStructConfig.h"
#if !defined(XMAP_REVERSE_ITERATOR_H)&& XMap_ON
#define XMAP_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XFunctionCallback.h"
XContainerTypeDeclare(XMap);
XContainerTypeDeclare(XPair);
typedef XPair* XMap_reverse_iterator;
XMap_reverse_iterator* XMap_rbegin(XMap* this_Map);
XMap_reverse_iterator* XMap_rend(XMap* this_Map);
XMap_reverse_iterator* XMap_reverse_iterator_add(XMap* this_Map, XMap_reverse_iterator* it);
void XMap_reverse_iterator_for_each(XMap* this_Map, XFor_each ForFunction, void* args);
#ifdef __cplusplus
}
#endif
#endif // !XMAP_REVERSE_ITERATOR_H
