#include"XSort.h"
#include"XAlgorithm.h"
void XReversed(void* LParray, const size_t nSize, const size_t TypeSize)
{
	char* left = LParray;
	char* right = left + (nSize - 1) * TypeSize;
	while (left < right)
	{
		XSwap(left, right, TypeSize);
		left += TypeSize;
		right -= TypeSize;
	}
}