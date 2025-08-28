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
#include "XHashSet_iterator.h"
// 默认初始容量
#define DEFAULT_CAPACITY 16
// 默认负载因子阈值
#define DEFAULT_LOAD_FACTOR 0.75f
#define XHASHSET_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XSetBase))       // XHashSet容器虚函数表大小

typedef struct XHashSet
{
    XSetBase      m_parent;   // 基本数据
    XHashFunc     m_hash;    // 哈希函数
} XHashSet;

XVtable* XHashSet_class_init();
// 开辟一个XHashSet,初始化
XHashSet* XHashSet_create(const size_t keyTypeSize, XHashFunc hash, XEquality KeyEquality, XLess KeyLess);
#define XHashSet_Create(keyType, KeyEquality,KeyLess) XHashSet_create(sizeof(keyType), XHashMap_murmur3_32, KeyEquality,KeyLess);
// 初始化 XHashSet
void XHashSet_init(XHashSet* this_set, const size_t keyTypeSize, XHashFunc hash, XEquality KeyEquality, XLess KeyLess);
#define XHashSet_insert_base                XSetBase_insert_base
#define XHashSet_insert_move_base           XSetBase_insert_move_base
#define XHashSet_erase_base                 XSetBase_erase_base
#define XHashSet_remove_base                XSetBase_remove_base
#define XHashSet_find_base                  XSetBase_find_base
#define XHashSet_contains                   XSetBase_contains
#define XHashSet_keys_base                  XSetBase_keys_base
#define XHashSet_copy_base				    XSetBase_copy_base	
#define XHashSet_move_base				    XSetBase_move_base	
#define XHashSet_deinit_base				XSetBase_deinit_base	
#define XHashSet_delete_base				XSetBase_delete_base	
#define XHashSet_clear_base				    XSetBase_clear_base	
#define XHashSet_isEmpty_base				XSetBase_isEmpty_base	
#define XHashSet_size_base				    XSetBase_size_base	
#define XHashSet_capacity_base			    XSetBase_capacity_base
#define XHashSet_swap_base				    XSetBase_swap_base	
#define XHashSet_typeSize_base			    XSetBase_typeSize_base

#ifdef __cplusplus
}
#endif
#endif // !XHashSet_H