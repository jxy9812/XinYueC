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
		XERROR_PRINTF("swap传入参数有问题\n");
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
			void* valMiddle = XMalloc_System(typeSize);
			if (valMiddle == NULL)
			{
				XERROR_PRINTF("交换函数创建p临时空间失败");
				exit(-1);
			}
			memcpy(valMiddle, valOne, typeSize);
			memcpy(valOne, valTwo, typeSize);
			memcpy(valTwo, valMiddle, typeSize);
			XFree_System(valMiddle);
		}
	}
}

void XStackRCopyXVector(const XStack* stack, XVector* vector)
{
#if XStack_ON
	size_t Size = XStack_size_base(stack);
	if (Size == 0)
		return;
	XVector_clear_base(vector);
	size_t TypeSize = XStack_typeSize_base(stack);
	char* pTail = XStack_top_base(stack);//数组末尾元素
	char* pHead = pTail- TypeSize*(Size-1);//数组头元素
	/*XVECTOR* v = (XVECTOR*)vector;
	v->object.m_data = XMalloc_System(Size * TypeSize);
	if (ISNULL(v->object.m_data, "")))
		return;
	v->object.m_capacity = Size;
	v->object.m_size = Size;
	for (size_t i = 0; i < Size; i++)
	{
		memcpy((char*)v->object.m_data + i * TypeSize, pHead + i * TypeSize, TypeSize);
	}*/
#else
	IS_ON_DEBUG(XStack_ON);
#endif
}

void XStackCopyXVector(const XStack* stack, XVector* vector)
{
#if XStack_ON
	size_t Size = XStack_size_base(stack);
	XVector_clear_base(vector);
	if (Size == 0)
		return;
	size_t TypeSize = XStack_typeSize_base(stack);
	char* pTop = XStack_top_base(stack);//栈顶元素（底层数组末尾元素）
	//从栈顶向栈底逐个压入目标向量，复用 XVector_push_back_1_base 的 COW 分离与扩容逻辑，
	//确保目标向量缓冲区被正确分配。原先直接向未分配的 XContainerSharedDataPtr 写入会导致段错误。
	for (size_t i = 0; i < Size; i++)
	{
		XVector_push_back_1_base(vector, pTop - i * TypeSize);
	}
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

uint16_t SwapEndian16(uint16_t data, uint8_t mode)
{
#if IS_BIG_ENDIAN
	if (mode) {
		// 大端模式：如果当前是大端，无需转换，直接返回原数据
		// 交换字节顺序
		return data;
	}
	else {
		// 小端模式：如果当前不是小端，需要转换
		return (data << 8) | (data >> 8);
	}
#else
	if (mode) {
		// 大端模式：如果当前不是大端，需要转换
		// 交换字节顺序
		return (data << 8) | (data >> 8);
	}
	else {
		// 小端模式：如果当前是小端，无需转换，直接返回原数据
		return data;
	}
}
#endif // IS_BIG_ENDIAN