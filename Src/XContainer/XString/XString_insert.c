#include"XString.h"
#include"XString_head.h"
#include"XVector.h"
#include"XContainerObject.h"
#include<string.h>
//尾插
void XString_append(struct XString* this_XString, const char* str)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return ;
	struct XVector* v = ((struct XSTRING*)this_XString)->_data;
	XString_insert(this_XString, XVector_size(v) - 1,str);
}
// 赋值
void XString_assign(struct XString* this_XString, const char* str)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	struct XVector* v = ((struct XSTRING*)this_XString)->_data;
	XString_clear(this_XString);
	XString_insert(this_XString,0, str);
}
// 第索引处开始插入字符串
void XString_insert(struct XString* this_XString, const int nSel, const char* str)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	if (str == NULL||nSel<0)
		return;
	struct XSTRING* string = (struct XSTRING*)this_XString;
	struct XVector* v = string->_data;
	XVector_insert(v, XVector_at(v, nSel), str, str + strlen(str) - 1);
	((struct XSTRING*)this_XString)->_size += XString_charNumber(str);
}