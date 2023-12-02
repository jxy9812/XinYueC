#include"XString.h"
#include"XString_head.h"
#include"XVector.h"
#include"XContainerObject.h"
// 返回索引处字符
char XString_at(const XString* this_XString, int nSel)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XSTRING* string = (struct XSTRING*)this_XString;
	return *((char*)XVector_at(string->_data,nSel));
}
// 返回字符串
char* XString_data(const XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XSTRING* string = (struct XSTRING*)this_XString;
	return string->_data->object._data;
}