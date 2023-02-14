#include"XPair.h"
#include"XContainerObject.h"
#include<stdlib.h>
#include<string.h>
XPair* XPair_init(const size_t firstTypeSize, const size_t secondTypeSize)
{
	if (firstTypeSize == 0 || secondTypeSize == 0)
	{
		printf("有类型设置错误");
		return NULL;
	}
	XPair* this_pair = (XPair*)malloc(sizeof(XPair));
	if (isNULL(isNULLInfo(this_pair, "初始化pair结构体失败")))
		return NULL;
	this_pair->first = calloc(1,firstTypeSize);//开辟的同时初始化为零
	if (isNULL(isNULLInfo(this_pair->first, "初始化pair-first失败")))
		return NULL;
	this_pair->second = calloc(1,secondTypeSize);
	if (isNULL(isNULLInfo(this_pair->second, "初始化pair-second失败")))
		return NULL;
	return this_pair;
}