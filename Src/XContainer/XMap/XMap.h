#include"XContainerObject.h"
#if !defined(XMAP_H)&& XMap_ON
#define XMAP_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XMapBase.h"
#include"XMap_Iterator.h"
#include"XMap_reverse_iterator.h"
#include"XLess.h"
typedef struct XVector XVector;
typedef struct XPair XPair;
#define XMAP_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XMapBase))       //XMap容器虚函数表大小
typedef struct XMap
{
	XMapBase m_parent;//基本数据
	XLess m_KeyLess;//key小于比较函数
}XMap;
XVtable* XMap_class_init();
//开辟一个Map,初始化
XMap* XMap_create(const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess/*, XEquality ValEquality*/);
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
//释放内存
#define XMap_delete_base				XMapBase_delete_base
//清空，不是释放内存
#define XMap_clear_base					XMapBase_clear_base
//检测是否为空，空为真 O(1)
#define XMap_isEmpty_base				XMapBase_isEmpty_base
//返回元素的个数 O(1)
#define XMap_getSize_base				XMapBase_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XMap_getCapacity_base			XMapBase_getCapacity_base
//交换两个同类型向量的数据
#define XMap_swap_base					XMapBase_swap_base
//返回元素类型字节大小
#define XMap_getTypeSize_base			XMapBase_getTypeSize_base

XMap* XMap_create_XStringXVariant();
#ifdef __cplusplus
}
#endif
#endif// !XMap_H
