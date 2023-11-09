#ifndef XPAIR_H
#define XPAIR_H
#include<stdio.h>
//#include"XVector.h"
typedef struct XPair//pair是将2个数据组合成一组数据，当需要这样的需求时就可以使用pair
{
	void* first;//第一组数据
	size_t firstTypeSize;//第一组数据类型大小
	void* second;//第二组数据
	size_t keyTypeSize;//第二组数据类型大小
}XPair;
//开辟一个XPair,初始化
#define XPair_Init(firstType,secondType) XPair_init(sizeof(firstType),sizeof(secondType))
XPair* XPair_init(const size_t firstTypeSize, const size_t keyTypeSize);
//插入数据
#define XPair_Insert(this_pair,firstData,secondData) XPair_insert(this_pair,&firstData,&secondData)
void XPair_insert(XPair* this_pair, void* firstData, void* secondData);
//插入数据第一个
#define XPair_InsertFirst(this_pair,firstData) XPair_insert(this_pair,&firstData)
void XPair_insertFirst(XPair* this_pair, void* firstData);
//插入数据第二个
#define XPair_InsertSecond(this_pair,secondData) XPair_insert(this_pair,&secondData)
void XPair_insertSecond(XPair* this_pair, void* secondData);
//获取第一个数据
#define XPair_First(this_pair,firstType) (*(firstType*)XPair_first(this_pair))
void* XPair_first(XPair* this_pair);
//获取第二个数据
#define XPair_Second(this_pair,secondType) (*(secondType*)XPair_second(this_pair))
void* XPair_second(XPair* this_pair);
//释放
void XPair_free(XPair* this_pair);
#endif