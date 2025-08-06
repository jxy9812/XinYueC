#include"XContainerObject.h"
#if !defined(XMAP_H)&& XMap_ON
#define XMAP_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XMapBase.h"
#include"XMap_iterator.h"
#include"XMap_reverse_iterator.h"
typedef struct XVector XVector;
typedef struct XPair XPair;
#define XMAP_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XMapBase))       //XMap容器虚函数表大小
typedef struct XMap
{
	XMapBase m_parent;//基本数据
}XMap;
XVtable* XMap_class_init();
//开辟一个Map,初始化
XMap* XMap_create(const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess);
#define XMap_Create(keyType,valType,KeyEquality,KeyLess) XMap_create(sizeof(keyType),sizeof(valType),KeyEquality,KeyLess)
//初始化 XMap
void XMap_init(XMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess);
//Map插入数据
#define XMap_insert_base				XMapBase_insert_base
#define XMap_Insert_Base				XMapBase_Insert_Base
#define XMap_erase_base					XMapBase_erase_base
#define XMap_remove_base				XMapBase_remove_base
#define XMap_Remove_Base				XMapBase_Remove_Base
#define XMap_value_base					XMapBase_value_base
#define XMap_Value_Base					XMapBase_Value_Base
#define XMap_find_base					XMapBase_find_base
#define XMap_contains					XMapBase_contains
#define XMap_copy_base					XMapBase_copy_base	
#define XMap_move_base					XMapBase_move_base	
#define XMap_deinit_base				XMapBase_deinit_base	
#define XMap_delete_base				XMapBase_delete_base	
#define XMap_clear_base					XMapBase_clear_base	
#define XMap_isEmpty_base				XMapBase_isEmpty_base	
#define XMap_size_base					XMapBase_size_base	
#define XMap_capacity_base				XMapBase_capacity_base
#define XMap_swap_base					XMapBase_swap_base	
#define XMap_typeSize_base				XMapBase_typeSize_base

XVariantMap* XMap_create_XVariantMap();
#ifdef __cplusplus
}
#endif
#endif// !XMap_H
