#include"XAlgorithm.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
void swap(void* valOne, void* valTwo, const int typeSize)//交换任意数据类型的函数
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
			void* valMiddle = malloc(typeSize);
			if (valMiddle == NULL)
			{
				perror("交换函数创建p临时空间失败");
				exit(-1);
			}
			memcpy(valMiddle, valOne, typeSize);
			memcpy(valOne, valTwo, typeSize);
			memcpy(valTwo, valMiddle, typeSize);
			free(valMiddle);
		}
	}
}

