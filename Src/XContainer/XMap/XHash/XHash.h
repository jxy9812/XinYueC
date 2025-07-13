#include"XContainerObject.h"
#if !defined(XHASHMAP_H)&& XHash_ON
#define XHASHMAP_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
#include"XMapBase.h"
#include"XHashFunc.h"
#include"XHash_Iterator.h"
// 默认初始容量
#define DEFAULT_CAPACITY 16
// 默认负载因子阈值
#define DEFAULT_LOAD_FACTOR 0.75f
#define XHASHMAP_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XMapBase))       //XHash容器虚函数表大小
//哈希节点
typedef struct XHashNode
{
	XPair* pair;//键值
	struct XHashNode* next;   // 指向下一个节点的指针
}XHashNode;
//#define XHashNode_KeyPtr(node)
typedef struct XHash
{
	XMapBase m_parent;//基本数据
	XHashFunc     m_hash;//哈希函数
}XHash;
XVtable* XHash_class_init();
//开辟一个XHash,初始化
XHash* XHash_create(const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XEquality KeyEquality);
#define XHash_Create(keyType,valType,hash,KeyEquality) XHash_create(sizeof(keyType),sizeof(valType),hash,KeyEquality)
	//初始化 XHash
void XHash_init(XHash* this_map, const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XEquality KeyEquality);
#define XHash_insert_base					XMapBase_insert_base
#define XHash_erase_base						XMapBase_erase_base
#define XHash_remove_base					XMapBase_remove_base
#define XHash_value_base						XMapBase_value_base
#define XHash_find_base						XMapBase_find_base
#define XHash_delete_base					XMapBase_delete_base
//清空，不是释放内存
#define XHash_clear_base						XMapBase_clear_base
//检测是否为空，空为真 O(1)
#define XHash_isEmpty_base					XMapBase_isEmpty_base
//返回元素的个数 O(1)						
#define XHash_getSize_base					XMapBase_getSize_base
//返回当前向量所能容纳的最大元素个数			 
#define XHash_getCapacity_base				XMapBase_getCapacity_base
//交换两个同类型向量的数据					
#define XHash_swap_base						XMapBase_swap_base
//返回元素类型字节大小						
#define XHash_getTypeSize_base				XMapBase_getTypeSize_base

XHash* XHash_create_XStringXVariant();
#ifdef __cplusplus
}
#endif
#endif// !XMap_H
