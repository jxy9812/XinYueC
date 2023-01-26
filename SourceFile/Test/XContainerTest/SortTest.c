#include"XAlgorithm.h"
bool SortInt (const void* LPrevValue, const void* LNextValue)
{
	return (*((int*)LPrevValue)) < (*((int*)LNextValue));
}

void SortTest()
{
	int array[] = { 10,100,21,123,123,54,5,12,5,12,13,51,5,3 };
	int nSize = sizeof(array) / sizeof(array[0]);
	XShellSort(array,nSize,sizeof(int), SortInt);
	for (size_t i = 0; i < nSize; i++)
	{
		printf("%d\n", array[i]);
	}

}