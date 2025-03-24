#include"XPair.h"
#include"XContainerObject.h"
#include<stdlib.h>
#include<string.h>
XPair* XPair_new(const size_t firstTypeSize, const size_t secondTypeSize)
{
	if (firstTypeSize == 0 || secondTypeSize == 0)
	{
		printf("有类型设置错误");
		return NULL;
	}
	XPair* this_pair = (XPair*)XMemory_malloc(sizeof(XPair));
	this_pair->firstTypeSize = firstTypeSize;
	this_pair->secondTypeSize = secondTypeSize;
	if (ISNULL(this_pair, "初始化pair结构体失败"))
		return NULL;
	this_pair->first = calloc(1,firstTypeSize);//开辟的同时初始化为零
	if (ISNULL(this_pair->first, "初始化pair-first失败"))
		return NULL;
	this_pair->second = calloc(1,secondTypeSize);
	if (ISNULL(this_pair->second, "初始化pair-second失败"))
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
	if (ISNULL(this_pair, ""))
		return;
	if (ISNULL(firstData, ""))
		return;
	memcpy(this_pair->first, firstData, this_pair->firstTypeSize);
}

void XPair_insertSecond(XPair* this_pair, void* secondData)
{
	if (ISNULL(this_pair, ""))
		return;
	/*if (ISNULL(secondData, "")))
		return;*/
	if(secondData!=NULL)
	{
		memcpy(this_pair->second, secondData, this_pair->secondTypeSize);
	}
	else
	{
		memset(this_pair->second,0, this_pair->secondTypeSize);
	}
}
void* XPair_first(XPair* this_pair)
{
	if (ISNULL(this_pair, ""))
		return;
	return this_pair->first;
}
void* XPair_second(XPair* this_pair)
{
	if (ISNULL(this_pair, ""))
		return;
	return this_pair->second;
}
void XPair_free(XPair* this_pair)
{
	XMemory_free(this_pair->first);
	XMemory_free(this_pair->second);
	XMemory_free(this_pair);
}
