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
void XVariantList_init(XVariantList* list);
void XVariantList_push_front_base(XVariantList* list, XVariant* var);
void XVariantList_push_back_base(XVariantList* list, XVariant* var);
void XVariantList_insert_base(XVariantList* list, int64_t index, XVariant* var);
// 返回
XVariant* XVariantList_at_base(const XVariantList* list, int64_t index);
XVariant* XVariantList_front_base(const XVariantList* list);
XVariant* XVariantList_back_base(const XVariantList* list);
XVariant* XVariantList_find_base(const XVariantList* list, const XVariant* findVal);
#define XVariantList_delete_base									XVector_delete_base
#define XVariantList_clear_base										XVector_clear_base
#define	XVariantList_remove_base									XVector_remove_base
#define XVariantList_erase_base										XVector_erase_base
#define XVariantList_pop_back_base									XVector_pop_back_base
#define	XVariantList_pop_front_base									XVector_pop_front_base
#define	XVariantList_resize_base									XVector_resize_base
#define XVariantList_isEmpty_base									XVector_isEmpty_base
#define XVariantList_getSize_base									XVector_getSize_base
#define XVariantList_getCapacity_base								XVector_getCapacity_base
#define XVariantList_swap_base										XVector_swap_base
#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H