#ifndef XMAP_ITERATOR_H
#define XMAP_ITERATOR_H
typedef struct XMap XMap;
typedef struct XMap_Iterator
{
	char null;
}XMap_Iterator;
XMap_Iterator* XMap_begin(XMap* this_Map);
XMap_Iterator* XMap_end(XMap* this_Map);
XMap_Iterator* XMap_iterator_add(XMap* this_list, XMap_Iterator* it);
#endif