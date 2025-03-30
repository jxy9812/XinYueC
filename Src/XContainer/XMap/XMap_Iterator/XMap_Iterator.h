#include"XDataStructConfig.h"
#if !defined(XMAP_ITERATOR_H)&& XMap_ON
#define XMAP_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
typedef struct XMap XMap;
typedef struct XPair XPair;
typedef XPair* XMap_Iterator;
XMap_Iterator* XMap_begin(XMap* this_Map);
XMap_Iterator* XMap_end(XMap* this_Map);
XMap_Iterator* XMap_iterator_add(XMap* this_Map, XMap_Iterator* it);
void XMap_iterator_for_each(XMap* this_Map, XFor_each ForFunction, void* args);
#ifdef __cplusplus
}
#endif
#endif