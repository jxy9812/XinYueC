#include "XFuncCodeMap.h"
#include "XHashMap.h"
XFuncCodeMap* XFuncCodeMap_create(size_t codeSize, XCompare codeCompare)
{
	XHashMap* hash = XHashMap_create(codeSize, sizeof(XFuncCodeNode), XHash_xxhash64, codeCompare);
	return hash;
}

bool XFuncCodeMap_add(XFuncCodeMap* map, void* code, XFuncCodeCb cb, void* userData)
{
	if (map == NULL || cb == NULL)
		return false;
	XFuncCodeNode node= {cb,userData};
	XMapBase_insert_base(map, code, &node);
	return true;
}

bool XFuncCodeMap_remove(XFuncCodeMap* map, void* code)
{
	if (map == NULL )
		return false;
	XMapBase_remove_base(map,code);
	return true;
}

XFuncCodeNode* XFuncCodeMap_value(XFuncCodeMap* map, void* code)
{
	if (map == NULL)
		return NULL;
	return XMapBase_value_base(map,code);
}

void* XFuncCodeMap_createCode(XFuncCodeMap* map)
{
	if (map == NULL)
		return NULL;
	void* code = XMemory_malloc(map->m_keyTypeSize);
	return code;
}

void XFuncCodeMap_deleteCode(void* code)
{
	if (code)
		XMemory_free(code);
}
