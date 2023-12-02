#ifndef STRING_HEAD
#define STRING_HEAD
#include<stdio.h>
#include<stdbool.h>
struct XVECTOR;
struct XSTRING
{
	//插入函数
	void(*append)(struct XSTRING* this_XString,const char* str);//尾插
	void (*assign)(struct XSTRING* this_XString, const char* str);// 赋值
	void (*insert)(struct XSTRING* this_XString, const int nSel,const char* str);// 第索引处开始插入字符串
	//删除函数
	void (*pop_back)(struct XSTRING* this_XString);//尾删
	void (*erase)(struct XSTRING* this_XString, const int nSel, const int n);//删除索引处开始的n个字符
	void (*clear) (struct XSTRING* this_XString);//清空，释放内存
	//遍历函数
	char (*at)(const struct XSTRING* this_XString, int);// 返回索引处字符
	char* (*values)(const struct XSTRING* this_XString);// 返回字符串
	//查找函数
	int (*find_first_of)(const struct XSTRING* this_XString, const char* find);
	int (*find_last_of)(const struct XSTRING* this_XString, const char* find);
	int (*find_first_not_of)(const struct XSTRING* this_XString, const char* find);
	int (*find_last_not_of)(const struct XSTRING* this_XString, const char* find);
	//判断函数
	bool (*empty)(const struct XSTRING* this_XString);// 
	//大小函数
	int (*size)(const struct XSTRING* this_XString);//
	//int (*capacity)(const struct XSTRING* this_XString); //
	//其他函数
	void (*swap)(struct XSTRING* this_XStringOne, struct XSTRING* this_XStringTwo);//交换
	//释放容器
	void (*free)(const struct XSTRING* this_XString);
	struct XVector* _data;
	size_t _size;//当前容器内的字符个数
};
//判断是中文
bool XString_isChinese(const char c);
//返回字符串中字符数量，中文算一个
size_t XString_charNumber(const char* str);
//返回对应XVector的索引
size_t XString_XVectorNsel(const struct XSTRING* this_XString,const size_t nSel);
#endif 
