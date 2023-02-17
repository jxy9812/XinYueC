#include"XAlgorithm.h"
#include"XSort.h"
void XShellSort(void* LParray, const size_t nSize, const size_t TypeSize, XCompare compare )
{
	for (size_t gap = nSize; gap > 1;)
	{
		gap = gap / 3 + 1;
		InsertSort_gap(LParray, nSize, TypeSize, gap, compare);
	}
}