#include"XContainerObject.h"
#if !defined(XHASHMAP_H)&& XHashMap_ON
#define XHASHMAP_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
typedef struct XVector XVector;
#define XHASHMAP_VTABLE_SIZE (XCONTAINEROBJECT_VTABLE_SIZE+5)       //XHashMap容器虚函数表大小
//XHashMap虚函数表枚举
enum XHashMapEnum
{
	EXHashMap_Insert = EXContainerObject_Clear + 1,
	EXHashMap_Erase,
	EXHashMap_Remove,
	EXHashMap_Value,
	EXHashMap_Find,
};
typedef struct XHashMapNode
{
	void* key;           // 键的指针
	void* value;         // 值的指针
	struct XHashMapNode* next;   // 指向下一个节点的指针
}XHashMapNode;
//#define XHashMapNode_KeyPtr(node)
typedef struct XHashMap
{
	XContainerObject m_parent;//基本数据
	size_t m_keyTypeSize;//第二组数据类型大小
	XHash     m_hash;//哈希函数
	XEquality m_KeyEquality;//key的相等比较函数
}XHashMap;
XVtable* XHashMap_class_init();
//开辟一个XHashMap,初始化
XHashMap* XHashMap_create(const size_t keyTypeSize, const size_t valTypeSize, XHash hash, XEquality KeyEquality);
#define XHashMap_Create(keyType,valType,hash,KeyEquality) XMap_create(sizeof(keyType),sizeof(valType),hash,KeyEquality)
	//初始化 XHashMap
void XHashMap_init(XHashMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XHash hash, XEquality KeyEquality);
#define XHashMap_free_base				XContainerObject_free_base
//清空，不是释放内存
#define XHashMap_clear_base				XContainerObject_clear_base
//检测是否为空，空为真 O(1)
#define XHashMap_isEmpty_base			XContainerObject_isEmpty_base
//返回元素的个数 O(1)
#define XHashMap_getSize_base				XContainerObject_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XHashMap_getCapacity_base			XContainerObject_getCapacity_base
//交换两个同类型向量的数据
#define XHashMap_swap_base				XContainerObject_swap_base
//返回元素类型字节大小
#define XHashMap_getTypeSize_base		XContainerObject_getTypeSize_base
#ifdef __cplusplus
}
#endif
#endif// !XMap_H
