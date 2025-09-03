#include"XDataStructTest.h"
#if DEMOTEST
#include"XSort.h"
#include"XCompare.h"
#include"XPrintf.h"
void SortTest()
{
	int array[] = { 10,100,21,123,123,54,5,12,5,12,13,51,5,3 };
	int nSize = sizeof(array) / sizeof(array[0]);
	//XHeapSort(array,nSize,sizeof(int), XLess_int);
	XReversed(array, nSize, sizeof(int));
	for (size_t i = 0; i < nSize; i++)
	{
		XPrintf("%d ", array[i]);
	}
	XPrintf("\n");
}

#endif