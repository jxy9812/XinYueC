#include"XDataStructConfig.h"
#if !defined(XSTRINGLIST_H)&& XStringList_ON
#define XSTRINGLIST_H
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
void XStringList_init(XStringList* this_stringVector);
#define XStringList_push_front_base								XVector_push_front_base
#define XStringList_push_front_move_base						XVector_push_front_move_base
void XStringList_push_front_c_str(XStringList* this_stringVector, const char* str);
#define XStringList_push_back_base								XVector_push_back_base
#define XStringList_push_back_move_base							XVector_push_back_move_base
void XStringList_push_back_c_str(XStringList* this_stringVector, const char* str);
#define XStringList_insert_base									XVector_insert
#define XStringList_insert_move_base							XVector_insert_move
void XStringList_insert_c_str(XStringList* this_stringVector, int64_t index, const char* str);
// 返回元素字符串
#define XStringList_at_base										XVector_at_base
#define XStringList_front_base									XVector_front_base
#define XStringList_back_base									XVector_back_base
//字符串拼接
XString* XStringList_join(const XStringList* this_stringVector, const char* separator);
#define XStringList_move_base									XVector_move_base
#define XStringList_copy_base									XVector_copy_base
#define XStringList_delete_base									XVector_delete_base
#define XStringList_clear_base									XVector_clear_base
#define	XStringList_remove_base									XVector_remove_base
#define XStringList_erase_base                                  XVector_erase_base
#define XStringList_pop_back_base								XVector_pop_back_base
#define	XStringList_pop_front_base								XVector_pop_front_base
#define	XStringList_resize_base									XVector_resize_base
#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H