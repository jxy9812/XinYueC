#include"XDataStructConfig.h"
#if !defined(XMAP_ITERATOR_H)&& XMap_ON
#define XMAP_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject_iterator.h"
#include"XFunctionCallback.h"
XContainerTypeDeclare(XMap);
XContainerTypeDeclare(XPair);
typedef XPair* XMap_iterator;
XMap_iterator* XMap_begin(XMap* this_Map);
XMap_iterator* XMap_end(XMap* this_Map);
XMap_iterator* XMap_iterator_add(XMap* this_Map, XMap_iterator* it);
void XMap_iterator_for_each(XMap* this_Map, XFor_each ForFunction, void* args);
#ifdef __cplusplus
}
#endif
#endif