#ifndef XMAP_REVERSE_ITERATOR_H
#define XMAP_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
typedef struct XMap XMap;
typedef struct XPair XPair;
typedef XPair* XMap_reverse_iterator;
XMap_reverse_iterator* XMap_rbegin(XMap* this_Map);
XMap_reverse_iterator* XMap_rend(XMap* this_Map);
XMap_reverse_iterator* XMap_reverse_iterator_add(XMap* this_Map, XMap_reverse_iterator* it);
void XMap_reverse_iterator_for_each(XMap* this_Map, XFor_each ForFunction, void* args);
#ifdef __cplusplus
}
#endif
#endif // !XMAP_REVERSE_ITERATOR_H
