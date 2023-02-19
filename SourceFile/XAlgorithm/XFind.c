#include"XFind.h"
#include"XContainerObject.h"
void* XBinarySearch(void* data, size_t n, size_t TypeSize, XLess less, XEquality equality, void* findVal)
{
	{
		if (isNULL(isNULLInfo(data, "")))
			return NULL;
		if (isNULL(isNULLInfo(n, "")))
			return NULL;
		if (isNULL(isNULLInfo(less, "")))
			return NULL;
		if (isNULL(isNULLInfo(equality, "")))
			return NULL;
		if (isNULL(isNULLInfo(findVal, "")))
			return NULL;
	}
	size_t nSel_Left=0;
	size_t nSel_Right = n-1;
	size_t nSel = 0;
	char* LPcurrt = NULL;
	while (nSel_Left<= nSel_Right)
	{
		nSel = (nSel_Left + nSel_Right) / 2;
		LPcurrt = (char*)data + nSel * TypeSize;
		//相等找到了
		if (equality(LPcurrt, findVal))
			return LPcurrt;
		//当前值小
		if(less(LPcurrt, findVal))
		{
			nSel_Left = nSel + 1;
		}
		else
		{
			nSel_Right = nSel - 1;
		}
	}
	return NULL;
}