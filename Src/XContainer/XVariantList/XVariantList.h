#include"XDataStructConfig.h"
#if !defined(XVARIANTLIST_H)&& XVariantList_ON
#define XVARIANTLIST_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include"XVector.h"
#include"XVariant.h"
#include"XVariantList_iterator.h"
#include"XVariantList_reverse_iterator.h"
#define XVARIANTLIST_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XVariantList))       //XVariantList容器虚函数表大小
//XMap虚函数表枚举
XCLASS_DEFINE_BEGING(XVariantList)
XCLASS_DEFINE_EXTEND_END(XVariantList, XVector)

typedef struct XVariantList
{
	XVector m_vector;
}XVariantList;
//初始化类
XVtable* XVariantList_class_init();
XVariantList* XVariantList_create();
XVariantList* XVariantList_create_copy(const XVariantList* other);
XVariantList* XVariantList_create_move(XVariantList* other);
void XVariantList_init(XVariantList* list);
#define XVariantList_push_front_base								XVector_push_front_base
#define XVariantList_push_front_move_base							XVector_push_front_move_base
#define XVariantList_push_back_base									XVector_push_back_base
#define XVariantList_push_back_move_base							XVector_push_back_move_base
#define XVariantList_insert											XVector_insert
#define XVariantList_insert_move									XVector_insert_move
#define XVariantList_at_base										XVector_at_base
#define XVariantList_front_base										XVector_front_base
#define XVariantList_back_base										XVector_back_base
#define XVariantList_find_base										XVector_find_base
#define	XVariantList_remove_base									XVector_remove_base
#define XVariantList_erase_base										XVector_erase_base
#define XVariantList_pop_back_base									XVector_pop_back_base
#define	XVariantList_pop_front_base									XVector_pop_front_base
#define	XVariantList_resize_base									XVector_resize_base

#define XVariantList_copy_base										XVector_copy_base	
#define XVariantList_move_base										XVector_move_base	
#define XVariantList_deinit_base									XVector_deinit_base	
#define XVariantList_delete_base									XVector_delete_base	
#define XVariantList_clear_base										XVector_clear_base	
#define XVariantList_isEmpty_base									XVector_isEmpty_base	
#define XVariantList_size_base										XVector_size_base	
#define XVariantList_capacity_base									XVector_capacity_base
#define XVariantList_swap_base										XVector_swap_base	
#define XVariantList_typeSize_base									XVector_typeSize_base
#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H