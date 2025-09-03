#include"XContainerObject.h"
#if !defined(XMAP_H)&& XSet_ON
#define XMAP_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XSetBase.h"
#include"XSet_iterator.h"
#include"XSet_reverse_iterator.h"
typedef struct XVector XVector;
#define XSET_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XSetBase))       //XSet容器虚函数表大小
typedef struct XSet
{
	XSetBase m_parent;//基本数据
}XSet;
XVtable* XSet_class_init();
//开辟一个Map,初始化
XSet* XSet_create(const size_t keyTypeSize, XCompare compare);
#define XSet_Create(keyType,compare) XSet_create(sizeof(keyType),compare)
//初始化 XSet
void XSet_init(XSet* this_map, const size_t keyTypeSize, XCompare compare);
//Map插入数据
#define XSet_insert_base				XSetBase_insert_base
#define XSet_Insert_Base				XSetBase_Insert_Base
#define XSet_insert_move_base			XSetBase_insert_move_base
#define XSet_erase_base					XSetBase_erase_base
#define XSet_remove_base				XSetBase_remove_base
#define XSet_Remove_Base				XSetBase_Remove_Base
#define XSet_find_base					XSetBase_find_base
#define XSet_contains					XSetBase_contains
#define XSet_keys_base					XSetBase_keys_base
#define XSet_copy_base				    XSetBase_copy_base	
#define XSet_move_base				    XSetBase_move_base	
#define XSet_deinit_base				XSetBase_deinit_base	
#define XSet_delete_base				XSetBase_delete_base	
#define XSet_clear_base				    XSetBase_clear_base	
#define XSet_isEmpty_base				XSetBase_isEmpty_base	
#define XSet_size_base					XSetBase_size_base	
#define XSet_capacity_base				XSetBase_capacity_base
#define XSet_swap_base				    XSetBase_swap_base	
#define XSet_typeSize_base				XSetBase_typeSize_base

#ifdef __cplusplus
}
#endif
#endif// !XSet_H
