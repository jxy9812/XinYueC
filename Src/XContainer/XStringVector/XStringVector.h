#include"XDataStructConfig.h"
#if !defined(XSTRINGVECTOR_H)&& XStringVector_ON
#define XSTRINGVECTOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include"XString.h"
#include"XStringVector_Iterator.h"
#include"XStringVector_reverse_iterator.h"
#define XSTRINGVECTOR_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XStringVector))       //XStringVector容器虚函数表大小
//XMap虚函数表枚举
XCLASS_DEFINE_BEGING(XStringVector)
XCLASS_DEFINE_EXTEND_END(XStringVector, XVector)
typedef struct XStringVector
{
	XVector m_vector;
}XStringVector;
//初始化类
XVtable* XStringVector_class_init();
//开辟一个字符串数组,初始化  实际等同于XVector_Create(XString*);
XStringVector* XStringVector_create();
void StringVector_init(XStringVector* this_stringVector);
void XStringVector_push_front_base(XStringVector* this_stringVector,XString*string);
void XStringVector_push_front_c_str(XStringVector* this_stringVector, const char* str);
void XStringVector_push_back_base(XStringVector* this_stringVector,XString* string);
void XStringVector_push_back_c_str(XStringVector* this_stringVector, const char* str);
void XStringVector_insert_base(XStringVector* this_stringVector, int64_t index, XString* string);
void XStringVector_insert_c_str(XStringVector* this_stringVector, int64_t index, const char* str);
// 返回元素字符串
XString* XStringVector_at_base(const XStringVector* this_stringVector, int64_t index);
XString* XStringVector_front_base(const XStringVector* this_stringVector);
XString* XStringVector_back_base(const XStringVector* this_stringVector);
//字符串拼接
XString* XStringVector_join(const XStringVector* this_stringVector, const char* separator);
#define XStringVector_delete_base									XVector_delete_base
#define XStringVector_clear_base									XVector_clear_base
#define	XStringVector_remove_base									XVector_remove_base
#define XStringVector_erase_base                                    XVector_erase_base
#define XStringVector_pop_back_base									XVector_pop_back_base
#define	XStringVector_pop_front_base								XVector_pop_front_base
#define	XStringVector_resize_base									XVector_resize_base
#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H