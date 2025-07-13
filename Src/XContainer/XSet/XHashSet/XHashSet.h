#include "XContainerObject.h"
#if !defined(XHASHSET_H) && XHashSet_ON
#define XHASHSET_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XFunctionCallback.h"
#include "XSetBase.h"
#include "XHashFunc.h"
#include "XEquality.h"
#include "XHashSet_Iterator.h"
// 默认初始容量
#define DEFAULT_CAPACITY 16
// 默认负载因子阈值
#define DEFAULT_LOAD_FACTOR 0.75f
#define XHASHSET_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XSetBase))       // XHashSet容器虚函数表大小
// 哈希节点
typedef struct XHashSetNode
{
    void* key;           // 键
    struct XHashSetNode* next;   // 指向下一个节点的指针
} XHashSetNode;

typedef struct XHashSet
{
    XSetBase m_parent;   // 基本数据
    XHashFunc     m_hash;    // 哈希函数
} XHashSet;

XVtable* XHashSet_class_init();
// 开辟一个XHashSet,初始化
XHashSet* XHashSet_create(const size_t keyTypeSize, XHashFunc hash, XEquality KeyEquality);
#define XHashSet_Create(keyType, hash, KeyEquality) XHashSet_create(sizeof(keyType), hash, KeyEquality);
// 初始化 XHashSet
void XHashSet_init(XHashSet* this_set, const size_t keyTypeSize, XHashFunc hash, XEquality KeyEquality);
#define XHashSet_insert_base XSetBase_insert_base
#define XHashSet_erase_base XSetBase_erase_base
#define XHashSet_remove_base XSetBase_remove_base
#define XHashSet_find_base XSetBase_find_base
#define XHashSet_delete_base XSetBase_delete_base
    // 清空，不是释放内存
#define XHashSet_clear_base XSetBase_clear_base
// 检测是否为空，空为真 O(1)
#define XHashSet_isEmpty_base XSetBase_isEmpty_base
// 返回元素的个数 O(1)
#define XHashSet_getSize_base XSetBase_getSize_base
// 返回当前向量所能容纳的最大元素个数
#define XHashSet_getCapacity_base XSetBase_getCapacity_base
// 交换两个同类型向量的数据
#define XHashSet_swap_base XSetBase_swap_base
// 返回元素类型字节大小
#define XHashSet_getTypeSize_base XSetBase_getTypeSize_base

#ifdef __cplusplus
}
#endif
#endif // !XHashSet_H