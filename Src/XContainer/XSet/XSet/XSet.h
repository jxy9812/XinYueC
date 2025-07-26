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
XSet* XSet_create(const size_t keyTypeSize, XEquality KeyEquality, XLess KeyLess);
#define XSet_Create(keyType,KeyEquality,KeyLess) XSet_create(sizeof(keyType),KeyEquality,KeyLess)
//初始化 XSet
void XSet_init(XSet* this_map, const size_t keyTypeSize, XEquality KeyEquality, XLess KeyLess);
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
//释放内存
#define XSet_delete_base				XSetBase_delete_base
//清空，不是释放内存
#define XSet_clear_base					XSetBase_clear_base
//检测是否为空，空为真 O(1)
#define XSet_isEmpty_base				XSetBase_isEmpty_base
//返回元素的个数 O(1)
#define XSet_getSize_base				XSetBase_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XSet_getCapacity_base			XSetBase_getCapacity_base
//交换两个同类型向量的数据
#define XSet_swap_base					XSetBase_swap_base
//返回元素类型字节大小
#define XSet_getTypeSize_base			XSetBase_getTypeSize_base

#ifdef __cplusplus
}
#endif
#endif// !XSet_H
