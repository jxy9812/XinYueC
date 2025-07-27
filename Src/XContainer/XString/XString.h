#include"XDataStructConfig.h"
#if !defined(STRING_H)&& XString_ON
#define STRING_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include"XContainerObject.h"
// UTF-16代理对范围定义
#define UTF16_HIGH_SURROGATE_START 0xD800
#define UTF16_HIGH_SURROGATE_END   0xDBFF
#define UTF16_LOW_SURROGATE_START  0xDC00
#define UTF16_LOW_SURROGATE_END    0xDFFF

#define XSTRING_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XString))       //XString容器虚函数表大小
//虚函数表枚举
XCLASS_DEFINE_BEGING(XString)
XCLASS_DEFINE_ENUM(XString, Resize) = XCLASS_VTABLE_GET_SIZE(XContainerObject),
XCLASS_DEFINE_ENUM(XString, Push_Front),
XCLASS_DEFINE_ENUM(XString, Push_Back),
XCLASS_DEFINE_ENUM(XString, Insert),
XCLASS_DEFINE_ENUM(XString, append),
XCLASS_DEFINE_ENUM(XString, Pop_Front),
XCLASS_DEFINE_ENUM(XString, Pop_Back),
XCLASS_DEFINE_ENUM(XString, Erase),
XCLASS_DEFINE_ENUM(XString, Assign),
XCLASS_DEFINE_ENUM(XString, Remove),
XCLASS_DEFINE_ENUM(XString, Rcopy),
XCLASS_DEFINE_ENUM(XString, At),
XCLASS_DEFINE_ENUM(XString, Front),
XCLASS_DEFINE_ENUM(XString, Back),
XCLASS_DEFINE_ENUM(XString, Find),
XCLASS_DEFINE_ENUM(XString, Find_First_Of),
XCLASS_DEFINE_ENUM(XString, Find_Last_Of),
XCLASS_DEFINE_ENUM(XString, Find_First_Not_Of),
XCLASS_DEFINE_ENUM(XString, Find_Last_Not_Of),
XCLASS_DEFINE_END(XString)

typedef struct XString
{
	XContainerObject m_parent;
	bool m_is_shared;       // 是否为共享数据(用于优化复制操作)
	int* m_ref_count;       // 引用计数，用于共享数据管理
}XString;
//初始化类
XVtable* XString_class_init();
//初始化XString;
 XString* XString_create(const char* utf8_str);
 XString* XString_create_fmt(const char* format, ...);
 XString* XString_create_with_length(const char* utf8_str,size_t len);
 //初始化 XVector
void XString_init(XString* this_string, const char* utf8_str);
#define XString_Init(var,string)  XString _##var,*var=&_##var;XString_init(var,string)
// 赋值
void XString_append_base(XString* this_string, const char* utf8_str);
void XString_append_string(XString* this_string, const XString* string);
// 索引前开始插入字符串
void XString_insert_base(XString* this_string, const int64_t index, const char* utf8_str);
void XString_assign_base(XString* this_string, const char* utf8_str);
// 返回字符串
const char* XString_data(const XString* this_string);
#define XString_c_str		XString_data
//查找函数
int64_t XString_find_first_of(const XString* this_string, const char* utf8_subStr);
int64_t XString_find_last_of(const XString* this_string, const char* utf8_subStr);
int64_t XString_find_first_not_of(const XString* this_string, const char* utf8_subStr);
int64_t XString_find_last_not_of(const XString* this_string, const char* utf8_subStr);
//设置XString的大小，超过大小插入0值数据，小于删除数据
bool XString_resize_base(XString* this_string, size_t len);
//向字符串头增加一个字符
bool XString_push_front_base(XString* this_string, uint16_t c);
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

#define XString_copy_base								XContainerObject_copy_base	
#define XString_move_base								XContainerObject_move_base	
#define XString_deinit_base								XContainerObject_deinit_base	
#define XString_delete_base								XContainerObject_delete_base	
#define XString_clear_base								XContainerObject_clear_base	
#define XString_isEmpty_base							XContainerObject_isEmpty_base	
#define XString_getSize_base							XContainerObject_getSize_base	
#define XString_getCapacity_base						XContainerObject_getCapacity_base
#define XString_swap_base								XContainerObject_swap_base	
#define XString_getTypeSize_base						XContainerObject_getTypeSize_base

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
