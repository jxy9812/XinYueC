#include"XPair.h"
#include"XContainerObject.h"
#include<stdlib.h>
#include<string.h>
XPair* XPair_new(const size_t firstTypeSize, const size_t keyTypeSize)
{
	if (firstTypeSize == 0 || keyTypeSize == 0)
	{
		printf("有类型设置错误");
		return NULL;
	}
	XPair* this_pair = (XPair*)malloc(sizeof(XPair));
	this_pair->firstTypeSize = firstTypeSize;
	this_pair->keyTypeSize = keyTypeSize;
	if (isNULL(isNULLInfo(this_pair, "初始化pair结构体失败")))
		return NULL;
	this_pair->first = calloc(1,firstTypeSize);//开辟的同时初始化为零
	if (isNULL(isNULLInfo(this_pair->first, "初始化pair-first失败")))
		return NULL;
	this_pair->second = calloc(1,keyTypeSize);
	if (isNULL(isNULLInfo(this_pair->second, "初始化pair-second失败")))
		return NULL;
	return this_pair;
}

void XPair_insert(XPair* this_pair, void* firstData, void* secondData)
{
	XPair_insertFirst(this_pair, firstData);
	XPair_insertSecond(this_pair, secondData);
}

void XPair_insertFirst(XPair* this_pair, void* firstData)
{
	if (isNULL(isNULLInfo(this_pair, "")))
		return;
	if (isNULL(isNULLInfo(firstData, "")))
		return;
	memcpy(this_pair->first, firstData, this_pair->firstTypeSize);
}

void XPair_insertSecond(XPair* this_pair, void* secondData)
{
	if (isNULL(isNULLInfo(this_pair, "")))
		return;
	/*if (isNULL(isNULLInfo(secondData, "")))
		return;*/
	if(secondData!=NULL)
	{
		memcpy(this_pair->second, secondData, this_pair->keyTypeSize);
	}
	else
	{
		memset(this_pair->second,0, this_pair->keyTypeSize);
	}
}
void* XPair_first(XPair* this_pair)
{
	if (isNULL(isNULLInfo(this_pair, "")))
		return;
	return this_pair->first;
}
void* XPair_second(XPair* this_pair)
{
	if (isNULL(isNULLInfo(this_pair, "")))
		return;
	return this_pair->second;
}
void XPair_free(XPair* this_pair)
{
	free(this_pair->first);
	free(this_pair->second);
	free(this_pair);
}
