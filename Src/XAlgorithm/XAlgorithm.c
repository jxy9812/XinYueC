#include"XAlgorithm.h"
#include"XClass.h"
#include"XStack.h"
#include"XVector.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>
#ifdef _WIN32
#include<Windows.h>
void gotoxy(short x, short y) 
{
	COORD coord = { x, y };
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}
#endif // _Win32
void XSwap(void* valOne, void* valTwo, const int typeSize)//交换任意数据类型的函数
{
	if (valOne == NULL || valTwo == NULL || typeSize <= 0)
	{
		perror("swap传入参数有问题\n");
		return;
	}
	if (valOne != valTwo)
	{
		if (typeSize == sizeof(char))//一字节
		{
			char tmp = *(char*)valOne;
			*(char*)valOne = *(char*)valTwo;
			*(char*)valTwo = tmp;
		}
		else if (typeSize == sizeof(short int))//两字节
		{
			short int tmp = *(short int*)valOne;
			*(short int*)valOne = *(short int*)valTwo;
			*(short int*)valTwo = tmp;
		}
		else if (typeSize == sizeof(int))//四字节
		{
			int tmp = *(int*)valOne;
			*(int*)valOne = *(int*)valTwo;
			*(int*)valTwo = tmp;
		}
		else if (typeSize == sizeof(long long))//八字节
		{
			long long tmp = *(long long*)valOne;
			*(long long*)valOne = *(long long*)valTwo;
			*(long long*)valTwo = tmp;
		}
		else
		{
			void* valMiddle = XMemory_malloc(typeSize);
			if (valMiddle == NULL)
			{
				perror("交换函数创建p临时空间失败");
				exit(-1);
			}
			memcpy(valMiddle, valOne, typeSize);
			memcpy(valOne, valTwo, typeSize);
			memcpy(valTwo, valMiddle, typeSize);
			XMemory_free(valMiddle);
		}
	}
}

void XStackRCopyXVector(const XStack* stack, XVector* vector)
{
#if XStack_ON
	size_t Size = XStack_size(stack);
	if (Size == 0)
		return;
	XVector_clear(vector);
	size_t TypeSize = XStack_typeSize(stack);
	char* pTail = XStack_top(stack);//数组末尾元素
	char* pHead = pTail- TypeSize*(Size-1);//数组头元素
	/*XVECTOR* v = (XVECTOR*)vector;
	v->object._data = XMemory_malloc(Size * TypeSize);
	if (ISNULL(v->object._data, "")))
		return;
	v->object._capacity = Size;
	v->object._size = Size;
	for (size_t i = 0; i < Size; i++)
	{
		memcpy((char*)v->object._data + i * TypeSize, pHead + i * TypeSize, TypeSize);
	}*/
#else
	IS_ON_DEBUG(XStack_ON);
#endif
}

void XStackCopyXVector(const XStack* stack, XVector* vector)
{
#if XStack_ON
	size_t Size = XStack_size(stack);
	if (Size == 0)
		return;
	XVector_clear(vector);
	size_t TypeSize = XStack_typeSize(stack);
	char* pTail = XStack_top(stack);//数组末尾元素
	char* pHead = pTail - TypeSize * (Size - 1);//数组头元素
	//XVECTOR* v = (XVECTOR*)vector;
	//v->object._data = XMemory_malloc(Size* TypeSize);
	//if (ISNULL(v->object._data, "")))
	//	return;
	//v->object._capacity = Size;
	//v->object._size = Size;
	//for (size_t i = 0; i < Size; i++)
	//{
	//	memcpy((char*)v->object._data+ i * TypeSize, pTail - i * TypeSize, TypeSize);
	//}
#else
	IS_ON_DEBUG(XStack_ON);
#endif
}

void XDelay(const size_t msec)
{
	clock_t  time_front = clock();
	while (true)
	{
		clock_t time_after = clock();
		if (time_after - time_front > msec)
			break;
	}
}

