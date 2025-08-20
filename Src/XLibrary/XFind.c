#include"XFind.h"
#include"XContainerObject.h"
void* XBinarySearch(void* values, size_t n, size_t TypeSize, XLess less, XEquality equality, void* findVal)
{
	{
		if (ISNULL(values, ""))
			return NULL;
		if (ISNULL(n, ""))
			return NULL;
		if (ISNULL(less, ""))
			return NULL;
		if (ISNULL(equality, ""))
			return NULL;
		if (ISNULL(findVal, ""))
			return NULL;
	}
	size_t nSel_Left=0;
	size_t nSel_Right = n-1;
	size_t nSel = 0;
	char* LPcurrt = NULL;
	while (nSel_Left<= nSel_Right)
	{
		nSel = (nSel_Left + nSel_Right) / 2;
		LPcurrt = (char*)values + nSel * TypeSize;
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