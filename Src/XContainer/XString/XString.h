#ifndef STRING_H
#define STRING_H
#include<stdbool.h>
#include"XVector.h"
//XVector虚函数表
extern XVtable* XStringVtable;
//XVector虚函数表枚举
//enum XStringEnum
//{
//	EXString_Clear = EXContainerObject_Clear,
//	EXString_Resize,
//	EXString_Push_Front,
//	EXString_Push_Back,
//	EXString_Inserts,
//	EXString_Insert,
//	EXString_InsertArray,
//	EXString_Pop_Front,
//	EXString_Pop_Back,
//	EXString_Erase,
//	EXString_Remove,
//	EXString_Copy,
//	EXString_Rcopy,
//	EXString_At,
//	EXString_Front,
//	EXString_Back,
//	EXString_Find,
//	EXString_Sort
//};

typedef struct XString
{
	XVector vector;
}XString;
//初始化类
void XString_class_init();
//初始化XString;
 XString* XString_new();
 //初始化 XVector
void XString_init(XString* this_string);
//尾插
void XString_append(struct XString* this_string, const char* str);
// 赋值
void XString_assign(struct XString* this_string, const char* str);
// 第索引处开始插入字符串
void XString_insert(struct XString* this_string, const int nSel, const char* str);
//尾删
void XString_pop_back(struct XString* this_string);
//删除索引处开始的n个字符
void XString_erase(struct XString* this_string, const int nSel, const int n);
//清空字符串
void XString_clear (struct XString* this_string);
// 返回索引处字符
char XString_at(const struct XString* this_string, int nSel);
// 返回字符串
char* XString_data(const struct XString* this_string);
//查找函数
int XString_find_first_of(const struct XString* this_string, const char* find);
int XString_find_last_of(const struct XString* this_string, const char* find);
int XString_find_first_not_of(const struct XString* this_string, const char* find);
int XString_find_last_not_of(const struct XString* this_string, const char* find);
//判断函数
bool XString_empty(const struct XString* this_string);// 
//返回当前元素大小
int XString_size(const struct XString* this_string);//
////返回当前容器的最大容量
//int XString_capacity(const struct XString* this_string); //
//交换
void XString_swap(struct XString* this_stringOne, struct XString* this_stringTwo);
//释放容器
void XString_free(const struct XString* this_string);
#endif // !STRING_H
