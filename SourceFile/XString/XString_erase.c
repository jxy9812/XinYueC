#include"XString.h"
#include"XString_head.h"
#include"XVector.h"
#include"XVector_head.h"
#include"XContainerObject.h"
//尾删
void XString_pop_back(struct XString* this_XString)
{
	if (isObjectNULL(this_XString, "XString_pop_back"))
		return;
	struct XVector* v = ((struct XSTRING*)this_XString)->_data;
	int nSel = XVector_size(v) - 2;
	if (nSel < 0)
		return;
	int offset = 0;
	if ((*((char*)XVector_at(v, nSel))) & 0x8000)//是中文
		++offset;
	XVector_erase_int(v, nSel-offset, nSel);
}
//删除索引处开始的n个字符
void XString_erase(struct XString* this_XString, const int nSel, const int n)
{
	if (isObjectNULL(this_XString, "XString_erase"))
		return;
	if (nSel < 0 || n <= 0)
		return;
	struct XVector* v = ((struct XSTRING*)this_XString)->_data;


	XVector_erase_int(v, nSel, nSel+n-1);
}
//清空字符串
void XString_clear(struct XString* this_XString)
{
	if (isObjectNULL(this_XString, "XString_clear"))
		return;
	struct XVector* v = ((struct XSTRING*)this_XString)->_data;
	int right = XVector_size(v) - 2;
	if(right>=0)
	XVector_erase_int(v, 0, right);
}