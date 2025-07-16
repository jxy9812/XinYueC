#include "XContainerObject.h"
#if !defined(XSETBASE_H) && XSet_ON
#define XSETBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XFunctionCallback.h"

#define XSETBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XSetBase))       // XSet容器虚函数表大小
// XSet虚函数表枚举
XCLASS_DEFINE_BEGING(XSetBase)
XCLASS_DEFINE_ENUM(XSetBase, Insert) = XCLASS_VTABLE_GET_SIZE(XContainerObject),
XCLASS_DEFINE_ENUM(XSetBase, Erase),
XCLASS_DEFINE_ENUM(XSetBase, Remove),
XCLASS_DEFINE_ENUM(XSetBase, Find),
XCLASS_DEFINE_ENUM(XSetBase, Keys),
XCLASS_DEFINE_END(XSetBase)
typedef struct XSetBase
{
    XContainerObject m_parent; // 基本数据
    size_t m_keyTypeSize;      // key数据类型大小
    XEquality m_KeyEquality;   // key的相等比较函数
} XSetBase;

XVtable* XSetBase_class_init();
// 初始化 XSet
void XSetBase_init(XSetBase* this_set, const size_t keyTypeSize, XEquality KeyEquality);
// Set插入数据
bool XSetBase_insert_base(XSetBase* this_set, const void* pvKey);
// Set删除数据
void XSetBase_erase_base(XSetBase* this_set, const void* pvKey);
// Set移除数据
bool XSetBase_remove_base(XSetBase* this_set, const void* pvKey);
// 查找数据，返回是否找到
bool XSetBase_find_base(XSetBase* this_set, const void* pvKey);
bool XSetBase_contains(XSetBase* this_set, const void* pvKey);
XVector* XSetBase_keys_base(const XSetBase* this_set);
// 释放内存
#define XSetBase_delete_base            XContainerObject_delete_base
// 清空，不是释放内存
#define XSetBase_clear_base             XContainerObject_clear_base
// 检测是否为空，空为真 O(1)
#define XSetBase_isEmpty_base           XContainerObject_isEmpty_base
// 返回元素的个数 O(1)
#define XSetBase_getSize_base           XContainerObject_getSize_base
// 返回当前向量所能容纳的最大元素个数
#define XSetBase_getCapacity_base       XContainerObject_getCapacity_base
// 交换两个同类型向量的数据
#define XSetBase_swap_base              XContainerObject_swap_base
// 返回元素类型字节大小
#define XSetBase_getTypeSize_base       XContainerObject_getTypeSize_base

#ifdef __cplusplus
}
#endif
#endif // !XSet_H