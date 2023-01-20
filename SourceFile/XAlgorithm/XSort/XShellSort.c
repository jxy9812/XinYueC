#include"XAlgorithm.h"
void XShellSort(void* LArray, const size_t nSize, const size_t TypeSize, bool(*Sort)(const void* LPrevValue, const void* LNextValue))
{
	for (size_t gap = nSize; gap > 1;)
	{
		gap = gap / 3 + 1;
		InsertSort_gap(LArray, nSize, TypeSize, gap, Sort);
	}
}