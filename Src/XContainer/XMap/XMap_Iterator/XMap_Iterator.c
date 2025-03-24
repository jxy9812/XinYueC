#include"XMap_Iterator.h"
#include"XMap.h"
#include"XVector.h"
#include"XRedBlackTree.h"
XMap_Iterator* XMap_begin(XMap* this_Map)
{
	XMap_updataIterator(this_Map);
	if (this_Map->itArray == NULL)
		return NULL;
	XVector_iterator* it = XVector_begin(this_Map->itArray);
	/*if(Vectorit!=NULL)
		return *(XPair**)Vectorit;*/
	return it;
}

XMap_Iterator* XMap_end(XMap* this_Map)
{
	return XVector_end(this_Map->itArray);
}

XMap_Iterator* XMap_iterator_add(XMap* this_Map, XMap_Iterator* it)
{
	XVector_iterator* iterator = XVector_iterator_add(this_Map->itArray,it);
	/*if (Vectorit != NULL)
		return *(XPair**)Vectorit;*/
	return iterator;
}

void XMap_iterator_for_each(XMap* this_Map, XFor_each ForFunction, void* args)
{
	for (XMap_Iterator* it = XMap_begin(this_Map); it != XMap_end(this_Map); it = XMap_iterator_add(this_Map, it))
	{
		ForFunction(it, args);
	}
}
