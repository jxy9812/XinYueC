#include"XString_head.h"
#include"XString.h"
#include"XVector.h"
#include"XContainerObject.h"
bool XString_isChinese(const char c)
{
	return (c & 0x8000);//是中文;
}
size_t XString_charNumber(const char* str)
{
	size_t sum = 0;
	for (size_t i = 0; i < strlen(str); i++)
	{
		if (XString_isChinese(str[i]))
			++i;
		++sum;
	}
	return sum;
}

size_t XString_XVectorNsel(const struct XSTRING* this_XString,const size_t nSel)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	if (nSel < 0)
		return -1;
	struct XVector* v = ((struct XSTRING*)this_XString)->_data;
	size_t VnSel = -1;
	for (size_t i = 0; i < XVector_size(v) - 1; i++)
	{
		char c = *((char*)XVector_at(v, i));
		if (XString_isChinese(c))
			++i;
		++VnSel;
		if (VnSel == nSel)
			return i;
	}
	return -1;
}
