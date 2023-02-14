#ifndef XPAIR_H
#define XPAIR_H
#include<stdio.h>
typedef struct XPair//pair是将2个数据组合成一组数据，当需要这样的需求时就可以使用pair
{
	void* first;//第一组数据
	void* second//第二组数据
}XPair;
//开辟一个XPair,初始化
#define XPair_Init(firstType,secondType) XPair_init(sizeof(firstType),sizeof(secondType))
XPair* XPair_init(const size_t firstTypeSize, const size_t secondTypeSize);
#endif