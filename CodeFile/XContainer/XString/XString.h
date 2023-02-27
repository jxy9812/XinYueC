#ifndef STRING_H
#define STRING_H
#include<stdbool.h>

typedef struct XString
{
	//插入函数
	void(*append)(struct XString* this_XString, const char* str);//尾插
	void (*assign)(struct XString* this_XString, const char* str);// 赋值
	void (*insert)(struct XString* this_XString, const int nSel, const char* str);// 第索引处开始插入字符串
	//删除函数
	void (*pop_back)(struct XString* this_XString);//尾删
	void (*erase)(struct XString* this_XString, const int nSel, const int n);//删除索引处开始的n个字符
	void (*clear) (struct XString* this_XString);//清空字符串
	//遍历函数
	char (*at)(const struct XString* this_XString, int);// 返回索引处字符
	char* (*value)(const struct XString* this_XString);// 返回字符串
	//查找函数
	int (*find_first_of)(const struct XString* this_XString, const char* find);
	int (*find_last_of)(const struct XString* this_XString, const char* find);
	int (*find_first_not_of)(const struct XString* this_XString, const char* find);
	int (*find_last_not_of)(const struct XString* this_XString, const char* find);
	//判断函数
	bool (*empty)(const struct XString* this_XString);// 
	//大小函数
	int (*size)(const struct XString* this_XString);//
	//int (*capacity)(const struct XString* this_XString); //
	//其他函数
	void (*swap)(struct XString* this_XStringOne, struct XString* this_XStringTwo);//交换
	//释放容器
	void (*free)(const struct XString* this_XString);
}XString;
//初始化XString;
struct XString* XString_init();
//尾插
void XString_append(struct XString* this_XString, const char* str);
// 赋值
void XString_assign(struct XString* this_XString, const char* str);
// 第索引处开始插入字符串
void XString_insert(struct XString* this_XString, const int nSel, const char* str);
//尾删
void XString_pop_back(struct XString* this_XString);
//删除索引处开始的n个字符
void XString_erase(struct XString* this_XString, const int nSel, const int n);
//清空字符串
void XString_clear (struct XString* this_XString);
// 返回索引处字符
char XString_at(const struct XString* this_XString, int nSel);
// 返回字符串
char* XString_data(const struct XString* this_XString);
//查找函数
int XString_find_first_of(const struct XString* this_XString, const char* find);
int XString_find_last_of(const struct XString* this_XString, const char* find);
int XString_find_first_not_of(const struct XString* this_XString, const char* find);
int XString_find_last_not_of(const struct XString* this_XString, const char* find);
//判断函数
bool XString_empty(const struct XString* this_XString);// 
//返回当前元素大小
int XString_size(const struct XString* this_XString);//
////返回当前容器的最大容量
//int XString_capacity(const struct XString* this_XString); //
//交换
void XString_swap(struct XString* this_XStringOne, struct XString* this_XStringTwo);
//释放容器
void XString_free(const struct XString* this_XString);
#endif // !STRING_H
