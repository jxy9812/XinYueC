#include"XPair.h"
#include"XContainerObject.h"
#include<stdlib.h>
#include<string.h>
XPair* XPair_create(const size_t firstTypeSize, const size_t secondTypeSize)
{
	if (firstTypeSize == 0 || secondTypeSize == 0)
	{
		printf("有类型设置错误");
		return NULL;
	}
	XPair* this_pair = (XPair*)XMemory_malloc(sizeof(XPair));
	this_pair->m_firstTypeSize = firstTypeSize;
	this_pair->m_secondTypeSize = secondTypeSize;
	if (ISNULL(this_pair, "初始化pair结构体失败"))
		return NULL;
	this_pair->m_first = XMemory_malloc(firstTypeSize);//开辟的同时初始化为零
	if (ISNULL(this_pair->m_first, "初始化pair-first失败"))
		return NULL;
	memset(this_pair->m_first,0, firstTypeSize);
	this_pair->m_second = XMemory_malloc(secondTypeSize);
	if (ISNULL(this_pair->m_second, "初始化pair-second失败"))
		return NULL;
	memset(this_pair->m_second, 0, secondTypeSize);
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
	memcpy(this_pair->m_first, firstData, this_pair->m_firstTypeSize);
}

void XPair_insertSecond(XPair* this_pair, void* secondData)
{
	if (ISNULL(this_pair, ""))
		return;
	/*if (ISNULL(secondData, "")))
		return;*/
	if(secondData!=NULL)
	{
		memcpy(this_pair->m_second, secondData, this_pair->m_secondTypeSize);
	}
	else
	{
		memset(this_pair->m_second,0, this_pair->m_secondTypeSize);
	}
}
void* XPair_first(XPair* this_pair)
{
	if (ISNULL(this_pair, ""))
		return;
	return this_pair->m_first;
}
void* XPair_second(XPair* this_pair)
{
	if (ISNULL(this_pair, ""))
		return;
	return this_pair->m_second;
}
void XPair_free(XPair* this_pair)
{
	XMemory_free(this_pair->m_first);
	XMemory_free(this_pair->m_second);
	XMemory_free(this_pair);
}
