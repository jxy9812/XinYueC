#include "XContainerObject.h"
#if !defined(XSETBASE_H) && XSet_ON
#define XSETBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
#include"XEquality.h"
#include"XLess.h"
#include"XSetBase_iterator.h"
//typedef struct XSetBase_iterator XSetBase_iterator;
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
    XEquality m_KeyEquality;   // key的相等比较函数
    XLess m_KeyLess;//key小于比较函数
} XSetBase;

XVtable* XSetBase_class_init();
// 初始化 XSet
void XSetBase_init(XSetBase* this_set, const size_t keyTypeSize, XEquality KeyEquality, XLess KeyLess);
// Set插入数据
bool XSetBase_insert_base(XSetBase* this_set, const void* pvKey);
#define XSetBase_Insert_Base(this_map,keyType,key,valType,Value) {keyType k=key;valType v=Value; XSetBase_insert_base(this_map,&k,&v);}
bool XSetBase_insert_move_base(XSetBase* this_set, const void* pvKey);
#define XSetBase_Insert_Move_Base(this_map,keyType,key,valType,Value) {keyType k=key;valType v=Value; XSetBase_insert_move_base(this_map,&k,&v);}

// Set删除数据
void XSetBase_erase_base(XSetBase* this_set, const XSetBase_iterator* it, XSetBase_iterator* next);
// Set移除数据
bool XSetBase_remove_base(XSetBase* this_set, const void* pvKey);
#define XSetBase_Remove_Base(this_map,keyType,key) {keyType k=key;XSet_remove_base(this_map,&k);}
// 查找数据，返回是否找到
bool XSetBase_find_base(XSetBase* this_set, const void* pvKey, XSetBase_iterator* it);
bool XSetBase_contains(XSetBase* this_set, const void* pvKey);
XVector* XSetBase_keys_base(const XSetBase* this_set);
#define XSetBase_copy_base				    XContainerObject_copy_base	
#define XSetBase_move_base				    XContainerObject_move_base	
#define XSetBase_deinit_base				XContainerObject_deinit_base	
#define XSetBase_delete_base				XContainerObject_delete_base	
#define XSetBase_clear_base				    XContainerObject_clear_base	
#define XSetBase_isEmpty_base				XContainerObject_isEmpty_base	
#define XSetBase_size_base				    XContainerObject_size_base	
#define XSetBase_capacity_base			    XContainerObject_capacity_base
#define XSetBase_swap_base				    XContainerObject_swap_base	
#define XSetBase_typeSize_base			    XContainerObject_typeSize_base

#ifdef __cplusplus
}
#endif
#endif // !XSet_H