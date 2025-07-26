#include"XDataStructConfig.h"
#if !defined(STRING_H)&& XString_ON
#define STRING_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include"XVector.h"
#define XSTRING_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XString))       //XString容器虚函数表大小
//XVector虚函数表枚举
XCLASS_DEFINE_BEGING(XString)
	EXString_Append = EXVector_append_Array_Copy,
	EXString_Assign= XCLASS_VTABLE_GET_SIZE(XVector),
	EXString_Find_First_Of,
	EXString_Find_Last_Of,
	EXString_Find_First_Not_Of,
	EXString_Find_Last_Not_Of,
XCLASS_DEFINE_END(XString)

typedef struct XString
{
	XVector m_vector;
}XString;
//初始化类
XVtable* XString_class_init();
//初始化XString;
 XString* XString_create(const char* string);
 XString* XString_create_fmt(const char* format, ...);
 XString* XString_create_with_length(const char* string,size_t len);
 //初始化 XVector
void XString_init(XString* this_string, const char* string);
#define XString_Init(var,string)  XString _##var,*var=&_##var;XString_init(var,string)
// 赋值
void XString_append_base(XString* this_string, const char* str);
void XString_append_string(XString* this_string, const XString* string);
// 索引前开始插入字符串
void XString_insert_base(XString* this_string, const int64_t index, const char* string);
void XString_assign_base(XString* this_string, const char* string);
// 返回字符串
const char* XString_data(const XString* this_string);
#define XString_c_str		XString_data
//查找函数
int64_t XString_find_first_of(const XString* this_string, const char* subStr);
int64_t XString_find_last_of(const XString* this_string, const char* subStr);
int64_t XString_find_first_not_of(const XString* this_string, const char* subStr);
int64_t XString_find_last_not_of(const XString* this_string, const char* subStr);
#define XString_copy_base								XVector_copy_base
#define XString_move_base								XVector_move_base
#define XString_deinit_base								XVector_deinit_base
//设置XString的大小，超过大小插入0值数据，小于删除数据
#define XString_resize_base								XVector_resize_base
//释放内存
#define XString_delete_base								XVector_delete_base
//清空vector的队列，不是释放内存
#define XString_clear_base								XVector_clear_base
//检测vector内是否为空，空为真 O(1)
#define XString_isEmpty_base							XVector_isEmpty_base
//返回vector内元素的个数 O(1)
#define XString_getSize_base							XVector_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XString_getCapacity_base						XVector_getCapacity_base
//交换两个同类型向量的数据
#define XString_swap_base								XVector_swap_base
//返回元素类型字节大小
#define XString_getTypeSize_base						XVector_getTypeSize_base
//设置XString的大小，实际大小自动+1存/0
#define XString_resize_base								XVector_resize_base
//向字符串头增加一个字符
#define XString_push_front_base(this_string,c)          XVector_Push_Front_Base(this_string,char,c)
//向字符串尾增加一个字符
#define XString_push_back_base(this_string,c)           XVector_Push_Back_Base(this_string,char,c)
//头删
#define XString_pop_front_base							XVector_pop_front_base
//尾删
#define XString_pop_back_base							XVector_pop_back_base
//删除指针处的字符
#define XString_erase_base								XVector_erase_base
//删除索引处开始的n个字符
#define XString_remove_base								XVector_remove_base
// 返回索引处字符
#define XString_at(this_string,index)					XVector_At_Base(this_string,index,char)

//转16进制显示
XString* XString_to16HexString(const uint8_t* data, size_t dataSize);
XStringList* XString_split(XString* this_string, const char* sep);
int  XString_toInt(XString* this_string);
long  XString_toLong(XString* this_string, int radix);
unsigned long  XString_toULong(XString* this_string, int radix);
long long  XString_toLongLong(XString* this_string, int radix);
unsigned long long  XString_toULongLong(XString* this_string, int radix);
float XString_toFloat(XString* this_string);
double XString_toDouble(XString* this_string);
#ifdef __cplusplus
}
#endif
#endif // !STRING_H
