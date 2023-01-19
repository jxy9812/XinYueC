#include"XString.h"
#include"XString_head.h"
#include"XVector.h"
#include"XVector_head.h"
#include"XContainerObject.h"
// 返回索引处字符
char XString_at(const struct XString* this_XString, int nSel)
{
	if (isObjectNULL(this_XString, "XString_at"))
		return NULL;
	struct XSTRING* string = (struct XSTRING*)this_XString;
	return *((char*)XVector_at(string->_data,nSel));
}
// 返回字符串
char* XString_data(const struct XString* this_XString)
{
	if (isObjectNULL(this_XString, "XString_data"))
		return NULL;
	struct XSTRING* string = (struct XSTRING*)this_XString;
	return string->_data->object._data;
}