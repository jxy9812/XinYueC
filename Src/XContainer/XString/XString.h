#include"XDataStructConfig.h"
#if !defined(STRING_H)&& XString_ON
#define STRING_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include"XVector.h"
//XVector虚函数表
extern XVtable* XStringVtable;
//XVector虚函数表枚举
enum XStringEnum
{
	EXString_Empty = EXContainerObject_Empty,
	EXString_Size = EXContainerObject_Size,
	EXString_Resize = EXVector_Resize,
	EXString_Push_Front,
	EXString_Push_Back,
	EXString_Inserts,
	EXString_Insert,
	EXString_InsertArray,
	EXString_Append = EXVector_append_Array,
	EXString_Pop_Front,
	EXString_Pop_Back,
	//EXString_Erase,
	//EXString_Remove,
	//EXString_Copy,
	//EXString_Rcopy,
	//EXString_At,
	//EXString_Front,
	//EXString_Back,
	//EXString_Find,
	//EXString_Sort,
	EXString_Assign=EXVector_Sort+1,
	EXString_Data,
	EXString_Find_First_Of,
	EXString_Find_Last_Of,
	EXString_Find_First_Not_Of,
	EXString_Find_Last_Not_Of,
};

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
//设置XString的大小，实际大小自动+1存/0
void XString_resize(XString* this_string, size_t len);
//头部增加一个字符
void XString_push_front(XString* this_string,char c);
//尾部增加一个字符
void XString_push_back(XString* this_string, char c);
//尾插
void XString_append(XString* this_string, const char* string);
// 赋值
void XString_assign(XString* this_string, const char* string);
// 索引前开始插入字符串
void XString_insert(XString* this_string, const int64_t index, const char* string);
//头删
void XString_pop_front(XString* this_string);
//尾删
void XString_pop_back(XString* this_string);
//删除指针处的字符
void XString_erase(XString* this_string, void* LpValue);
//删除索引处开始的n个字符
void XString_remove(XString* this_string, int64_t index, int64_t n);
//清空字符串
void XString_clear (XString* this_string);
// 返回索引处字符
char XString_at(const XString* this_string, int64_t index);
// 返回字符串
const char* XString_data(const XString* this_string);
//查找函数
int64_t XString_find_first_of(const XString* this_string, const char* subStr);
int64_t XString_find_last_of(const XString* this_string, const char* subStr);
int64_t XString_find_first_not_of(const XString* this_string, const char* subStr);
int64_t XString_find_last_not_of(const XString* this_string, const char* subStr);
//判断函数
bool XString_empty(const XString* this_string);// 
//返回当前字符串大小
size_t XString_size(const XString* this_string);//
////返回当前容器的最大容量
//int XString_capacity(const XString* this_string); //
//交换
void XString_swap(XString* this_stringOne, XString* this_stringTwo);
//释放容器
void XString_free(const XString* this_string);
#ifdef __cplusplus
}
#endif
#endif // !STRING_H
