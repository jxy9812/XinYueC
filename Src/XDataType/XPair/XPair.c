#include"XPair.h"
#include"XContainerObject.h"
//#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>
XPair* XPair_create(const size_t firstTypeSize, const size_t secondTypeSize)
{
	if (firstTypeSize == 0 || secondTypeSize == 0)
	{
		printf("有类型设置错误");
		return NULL;
	}
	XPair* this_pair = (XPair*)XMemory_malloc(sizeof(size_t)*2+ firstTypeSize+secondTypeSize);
	memset(this_pair,0, sizeof(size_t) * 2 + firstTypeSize + secondTypeSize);
	this_pair->m_firstTypeSize = firstTypeSize;
	this_pair->m_secondTypeSize = secondTypeSize;
	if (ISNULL(this_pair, "初始化pair结构体失败"))
		return NULL;
	return this_pair;
}

XPair* XPair_create_copy(const XPair* other)
{
	if (other == NULL)
		return NULL;
	XPair* pair = XPair_create(other->m_firstTypeSize, other->m_secondTypeSize);
	if(pair==NULL)
		return NULL;
	XPair_copy(pair,other);
	return pair;
}

XPair* XPair_create_move(XPair* other)
{
	if (other == NULL)
		return NULL;
	XPair* pair = XPair_create(other->m_firstTypeSize, other->m_secondTypeSize);
	if (pair == NULL)
		return NULL;
	XPair_move(pair, other);
	return pair;
}

void XPair_copy(XPair* this_pair, const XPair* copy)
{
	if (this_pair == NULL || copy == NULL||this_pair->m_firstTypeSize!=copy->m_firstTypeSize||this_pair->m_secondTypeSize!=copy->m_secondTypeSize)
		return;
	memcpy(&(this_pair->m_first), &(copy->m_first), copy->m_firstTypeSize + copy->m_secondTypeSize);
}

void XPair_move(XPair* this_pair, XPair* move)
{
	if (this_pair == NULL || move == NULL || this_pair->m_firstTypeSize != move->m_firstTypeSize || this_pair->m_secondTypeSize != move->m_secondTypeSize)
		return;
	memcpy(&(this_pair->m_first), &(move->m_first), move->m_firstTypeSize + move->m_secondTypeSize);
	memset(&(move->m_first),0, move->m_firstTypeSize + move->m_secondTypeSize);
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
	if(firstData)
		memcpy(&(this_pair->m_first), firstData, this_pair->m_firstTypeSize);
	else
		memset(&(this_pair->m_first), 0, this_pair->m_firstTypeSize);
}

void XPair_insertSecond(XPair* this_pair, void* secondData)
{
	if (ISNULL(this_pair, ""))
		return;
	if(secondData!=NULL)
	{
		memcpy(((uint8_t*)(&(this_pair->m_first))) + this_pair->m_firstTypeSize, secondData, this_pair->m_secondTypeSize);
	}
	else
	{
		memset(((uint8_t*)(&(this_pair->m_first))) + this_pair->m_firstTypeSize, 0, this_pair->m_secondTypeSize);
	}
}
void* XPair_first(XPair* this_pair)
{
	if (ISNULL(this_pair, ""))
		return;
	return &(this_pair->m_first);
}
void* XPair_second(XPair* this_pair)
{
	if (ISNULL(this_pair, ""))
		return;
	return ((uint8_t*)(&(this_pair->m_first))) + this_pair->m_firstTypeSize;
}
size_t XPair_getSize(XPair* this_pair)
{
	return ((char*)(&(this_pair->m_first))) - ((char*)this_pair) + this_pair->m_firstTypeSize + this_pair->m_secondTypeSize;
}
void XPair_delete(XPair* this_pair)
{
	XMemory_free(this_pair);
}

int32_t XPair_compare(const XPair* lhs, const XPair* rhs)
{
	if ((lhs->m_first == rhs->m_first)&& 
		(lhs->m_second == rhs->m_second)&&
		memcmp(XPair_first(lhs), XPair_first(rhs), lhs->m_first)==0&&
		memcmp(XPair_second(lhs), XPair_second(rhs), lhs->m_second)==0)
		return XCompare_Equality;
	return XCompare_Other;
}
