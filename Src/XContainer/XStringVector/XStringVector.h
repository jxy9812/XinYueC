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
//XStringVector虚函数表
//extern XVtable* XStringVectorVtable;

typedef struct XStringVector
{
	XVector m_vector;
}XStringVector;
//初始化类
//void XStringVector_class_init();
//开辟一个字符串数组,初始化  实际等同于XVector_New(XString*);
XStringVector* XStringVector_new();
//释放内存
void XStringVector_free(XStringVector* this_stringVector);
void XStringVector_push_front(XStringVector* this_stringVector,XString*string);
void XStringVector_push_front_c_str(XStringVector* this_stringVector, const char* str);
void XStringVector_push_back(XStringVector* this_stringVector,XString* string);
void XStringVector_push_back_c_str(XStringVector* this_stringVector, const char* str);
void XStringVector_insert(XStringVector* this_stringVector, int64_t index, XString* string);
void XStringVector_insert_c_str(XStringVector* this_stringVector, int64_t index, const char* str);
// 返回元素字符串
XString* XStringVector_at(const XStringVector* this_stringVector, int64_t index);
XString* XStringVector_front(const XStringVector* this_stringVector);
XString* XStringVector_back(const XStringVector* this_stringVector);
#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H