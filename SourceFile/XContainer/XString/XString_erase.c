#include"XString.h"
#include"XString_head.h"
#include"XVector.h"
#include"XVector_head.h"
#include"XContainerObject.h"
//删除索引处字符
bool XString_eraseOne(struct XString* this_XString, const int nSel)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return false;
	if (nSel < 0 )
		return false;
	struct XVector* v = ((struct XSTRING*)this_XString)->_data;
	size_t VnSel=XString_XVectorNsel(this_XString, nSel);
	if (VnSel == -1)
		return false;

	int offset = 0;
	if (XString_isChinese(*((char*)XVector_at(v, VnSel))))//是中文
		++offset;
	XVector_erase_int(v, VnSel - offset, VnSel);
	((struct XSTRING*)this_XString)->_size -= 1;
	return true;
}
//尾删
void XString_pop_back(struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	XString_eraseOne(this_XString, XString_size(this_XString) - 1);
}
//删除索引处开始的n个字符
void XString_erase(struct XString* this_XString, const int nSel, const int n)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	if (nSel < 0 || n <= 0)
		return;
	for (size_t i = 0; i < n; i++)
	{
		if (!XString_eraseOne(this_XString, nSel))
			break;
	}
}
//清空字符串
void XString_clear(struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	struct XVector* v = ((struct XSTRING*)this_XString)->_data;
	int right = XVector_size(v) - 2;
	if(right>=0)
	XVector_erase_int(v, 0, right);
	((struct XSTRING*)this_XString)->_size = 0;
}