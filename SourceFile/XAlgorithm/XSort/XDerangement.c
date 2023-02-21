#include"XSort.h"
#include"XAlgorithm.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>
//乱序
void XDerangement(void* LParray, const size_t nSize, const size_t TypeSize)
{
	srand((unsigned int)time(NULL));
	for (size_t i = nSize; i>1; i--)
	{
		size_t nSel = rand() % i;
		swap((char*)LParray+ nSel* TypeSize, (char*)LParray+(i-1)*TypeSize, TypeSize);
	}
}