#include "XMap_reverse_iterator.h"
#if XMap_ON
#include"XMap.h"
#include"XVector.h"
#include"XRedBlackTree.h"
XMap_reverse_iterator* XMap_rbegin(XMap* this_Map)
{
#if XVector_ON
	XMap_updataIterator(this_Map);
	if (this_Map->itArray == NULL)
		return NULL;
	XVector_reverse_iterator* it = XVector_rbegin(this_Map->itArray);

	return it;
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif;
}

XMap_reverse_iterator* XMap_rend(XMap* this_Map)
{
#if XVector_ON
	return XVector_rend(this_Map->itArray);
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif;
}

XMap_reverse_iterator* XMap_reverse_iterator_add(XMap* this_Map, XMap_reverse_iterator* it)
{
#if XVector_ON
	XVector_reverse_iterator* reverse_iterator = XVector_reverse_iterator_add(this_Map->itArray, it);
	return reverse_iterator;
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif;
}

void XMap_reverse_iterator_for_each(XMap* this_Map, XFor_each ForFunction, void* args)
{
	for (XMap_reverse_iterator* it = XMap_rbegin(this_Map); it != XMap_rend(this_Map); it = XMap_reverse_iterator_add(this_Map, it))
	{
		ForFunction(it, args);
	}
}

#endif