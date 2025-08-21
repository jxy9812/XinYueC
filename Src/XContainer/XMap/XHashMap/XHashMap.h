#include"XContainerObject.h"
#if !defined(XHASHMAP_H)&& XHashMap_ON
#define XHASHMAP_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
#include"XMapBase.h"
#include"XHashFunc.h"
#include"XHashMap_iterator.h"
// 默认初始容量
#define DEFAULT_CAPACITY 16
// 默认负载因子阈值
#define DEFAULT_LOAD_FACTOR 0.75f
#define XHASHMAP_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XMapBase))       //XHash容器虚

typedef struct XHashMap
{
	XMapBase	m_parent;//基本数据
	XHashFunc   m_hash;//哈希函数
}XHashMap;
XVtable* XHashMap_class_init();
//创建一个XHashMap,初始化
XHashMap* XHashMap_create(const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XEquality KeyEquality, XLess KeyLess);
XHashMap* XHashMap_create_copy(const XHashMap* other);
XHashMap* XHashMap_create_move(XHashMap* other);
#define XHashMap_Create(keyType,valType,KeyEquality,KeyLess) XHashMap_create(sizeof(keyType),sizeof(valType),XHashMap_murmur3_32,KeyEquality,KeyLess)

//初始化 XHash
void XHashMap_init(XHashMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XEquality KeyEquality, XLess KeyLess);
#define XHashMap_insert_base					XMapBase_insert_base
#define XHashMap_erase_base						XMapBase_erase_base
#define XHashMap_remove_base					XMapBase_remove_base
#define XHashMap_value_base						XMapBase_value_base
#define XHashMap_find_base						XMapBase_find_base
#define XHashMap_delete_base					XMapBase_delete_base
#define XHashMap_copy_base						XMapBase_copy_base	
#define XHashMap_move_base						XMapBase_move_base	
#define XHashMap_deinit_base					XMapBase_deinit_base	
#define XHashMap_delete_base					XMapBase_delete_base	
#define XHashMap_clear_base						XMapBase_clear_base	
#define XHashMap_isEmpty_base					XMapBase_isEmpty_base	
#define XHashMap_size_base					XMapBase_size_base	
#define XHashMap_capacity_base				XMapBase_capacity_base
#define XHashMap_swap_base						XMapBase_swap_base	
#define XHashMap_typeSize_base				XMapBase_typeSize_base

XVariantHashMap* XHashMap_create_XVariantHashMap();
#ifdef __cplusplus
}
#endif
#endif// !XMap_H
