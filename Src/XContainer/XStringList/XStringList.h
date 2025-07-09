#include"XDataStructConfig.h"
#if !defined(XSTRINGVECTOR_H)&& XStringList_ON
#define XSTRINGVECTOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include"XString.h"
#include"XStringList_Iterator.h"
#include"XStringList_reverse_iterator.h"
#define XSTRINGVECTOR_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XStringList))       //XStringList容器虚函数表大小
//XMap虚函数表枚举
XCLASS_DEFINE_BEGING(XStringList)
XCLASS_DEFINE_EXTEND_END(XStringList, XVector)
typedef struct XStringList
{
	XVector m_vector;
}XStringList;
//初始化类
XVtable* XStringList_class_init();
//开辟一个字符串数组,初始化  实际等同于XVector_Create(XString*);
XStringList* XStringList_create();
void StringVector_init(XStringList* this_stringVector);
void XStringList_push_front_base(XStringList* this_stringVector,XString*string);
void XStringList_push_front_c_str(XStringList* this_stringVector, const char* str);
void XStringList_push_back_base(XStringList* this_stringVector,XString* string);
void XStringList_push_back_c_str(XStringList* this_stringVector, const char* str);
void XStringList_insert_base(XStringList* this_stringVector, int64_t index, XString* string);
void XStringList_insert_c_str(XStringList* this_stringVector, int64_t index, const char* str);
// 返回元素字符串
XString* XStringList_at_base(const XStringList* this_stringVector, int64_t index);
XString* XStringList_front_base(const XStringList* this_stringVector);
XString* XStringList_back_base(const XStringList* this_stringVector);
//字符串拼接
XString* XStringList_join(const XStringList* this_stringVector, const char* separator);
#define XStringList_delete_base									XVector_delete_base
#define XStringList_clear_base									XVector_clear_base
#define	XStringList_remove_base									XVector_remove_base
#define XStringList_erase_base                                    XVector_erase_base
#define XStringList_pop_back_base									XVector_pop_back_base
#define	XStringList_pop_front_base								XVector_pop_front_base
#define	XStringList_resize_base									XVector_resize_base
#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H