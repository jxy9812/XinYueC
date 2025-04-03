#include"XMap_Iterator.h"
#if XMap_ON
#include"XMap.h"
#include"XVector.h"
#include"XRedBlackTree.h"
XMap_Iterator* XMap_begin(XMap* this_Map)
{
#if XVector_ON
	XMap_updataIterator(this_Map);
	if (this_Map->m_itArray == NULL)
		return NULL;
	XVector_iterator* it = XVector_begin(this_Map->m_itArray);
	/*if(Vectorit!=NULL)
		return *(XPair**)Vectorit;*/
	return it;
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif;
}

XMap_Iterator* XMap_end(XMap* this_Map)
{
#if XVector_ON
	return XVector_end(this_Map->m_itArray);
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif;
}

XMap_Iterator* XMap_iterator_add(XMap* this_Map, XMap_Iterator* it)
{
#if XVector_ON
	XVector_iterator* iterator = XVector_iterator_add(this_Map->m_itArray,it);
	/*if (Vectorit != NULL)
		return *(XPair**)Vectorit;*/
	return iterator;
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif;
}

void XMap_iterator_for_each(XMap* this_Map, XFor_each ForFunction, void* args)
{
	for (XMap_Iterator* it = XMap_begin(this_Map); it != XMap_end(this_Map); it = XMap_iterator_add(this_Map, it))
	{
		ForFunction(it, args);
	}
}


#endif