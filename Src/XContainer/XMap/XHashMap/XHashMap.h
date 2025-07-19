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
#define XHashMap_Create(keyType,valType,hash,KeyEquality,KeyLess) XHashMap_create(sizeof(keyType),sizeof(valType),hash,KeyEquality,KeyLess)
//初始化 XHash
void XHashMap_init(XHashMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XEquality KeyEquality, XLess KeyLess);
#define XHashMap_insert_base						XMapBase_insert_base
#define XHashMap_erase_base						XMapBase_erase_base
#define XHashMap_remove_base						XMapBase_remove_base
#define XHashMap_value_base						XMapBase_value_base
#define XHashMap_find_base							XMapBase_find_base
#define XHashMap_delete_base						XMapBase_delete_base
//清空，不是释放内存
#define XHashMap_clear_base						XMapBase_clear_base
//检测是否为空，空为真 O(1)
#define XHashMap_isEmpty_base						XMapBase_isEmpty_base
//返回元素的个数 O(1)						
#define XHashMap_getSize_base						XMapBase_getSize_base
//返回当前向量所能容纳的最大元素个数			 
#define XHashMap_getCapacity_base					XMapBase_getCapacity_base
//交换两个同类型向量的数据					
#define XHashMap_swap_base							XMapBase_swap_base
//返回元素类型字节大小						
#define XHashMap_getTypeSize_base					XMapBase_getTypeSize_base

XHashMap* XHashMap_create_XStringVariant();
#ifdef __cplusplus
}
#endif
#endif// !XMap_H
