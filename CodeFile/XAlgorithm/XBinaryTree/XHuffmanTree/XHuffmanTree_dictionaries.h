//哈夫曼树字典类型声明
#ifndef XHUFFMANTREE_DICTIONARIES_H
#define XHUFFMANTREE_DICTIONARIES_H
#include"XVector.h"
//字典数据(写入压缩的)
typedef struct DictionaryData
{
	char ch;//字符
	size_t count;//出现次数
	size_t codeSize;//编码大小
}DictionaryData;
//字典值(内容)
typedef struct DictionaryValue
{
	size_t count;//出现次数
	XVector* code;//哈夫曼编码
}DictionaryValue;

#endif