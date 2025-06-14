#include "XFuncCodeMap.h"
#include "XHashMap.h"
XFuncCodeMap* XFuncCodeMap_create()
{
	XHashMap* hash = XHashMap_Create(uint8_t, XFuncCodeNode,XHash_murmur3_32,XEquality_uint8_t);
	return hash;
}

bool XFuncCodeMap_add(XFuncCodeMap* map, uint8_t code, XFuncCodeCb cb, void* userData)
{
	if (map == NULL || cb == NULL)
		return false;
	XFuncCodeNode node= {cb,userData};
	XMapBase_insert_base(map, &code, &node);
	return true;
}

bool XFuncCodeMap_remove(XFuncCodeMap* map, uint8_t code)
{
	if (map == NULL )
		return false;
	XMapBase_remove_base(map,&code);
	return true;
}

XFuncCodeNode* XFuncCodeMap_value(XFuncCodeMap* map, uint8_t code)
{
	if (map == NULL)
		return NULL;
	return XMapBase_value_base(map,&code);
}

bool XFuncCodeMap_clear(XFuncCodeMap* map)
{
	if (map == NULL)
		return false;
	XMapBase_clear_base(map);
	return true;
}

void XFuncCodeMap_delete(XFuncCodeMap* map)
{
	if (map == NULL)
		return ;
	XMapBase_delete_base(map);
}
