#include"XContainerObject.h"
#if !defined(XMAPBASE_H)&& XMap_ON
#define XMAPBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XCompare.h"
#include"XPair.h"
#include"XMapBase_iterator.h"
//typedef struct XMapBase_iterator XMapBase_iterator;
#define XMAPBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XMapBase))       //XMap容器虚函数表大小
//XMap虚函数表枚举
XCLASS_DEFINE_BEGING(XMapBase)
XCLASS_DEFINE_ENUM(XMapBase,Insert)= XCLASS_VTABLE_GET_SIZE(XContainerObject),
XCLASS_DEFINE_ENUM(XMapBase,Erase),
XCLASS_DEFINE_ENUM(XMapBase,Remove),
XCLASS_DEFINE_ENUM(XMapBase,Value),
XCLASS_DEFINE_ENUM(XMapBase,Find), 
XCLASS_DEFINE_ENUM(XMapBase,Keys),
XCLASS_DEFINE_ENUM(XMapBase,Values),
XCLASS_DEFINE_END(XMapBase)
typedef struct XMapBase
{
	XContainerObject m_class;//基本数据
	size_t m_keyTypeSize;//key数据类型大小
	//XEquality m_KeyEquality;//key的相等比较函数
	//XLess m_KeyLess;//key小于比较函数
	XCDataCopyMethod m_keyCopyMethod;//key拷贝方法
	XCDataMoveMethod m_keyMoveMethod;//key移动方法
	XCDataDeinitMethod m_keyDeinitMethod;//key释放方法
}XMapBase;
XVtable* XMapBase_class_init();
//初始化 XMap
void XMapBase_init(XMapBase* this_map, const size_t keyTypeSize, const size_t valTypeSize, XCompare compare);
//Map插入数据
bool XMapBase_insert_base(XMapBase* this_map, const void* key, const void* pvValue);
#define XMapBase_Insert_Base(this_map,keyType,key,valType,Value) {keyType k=key;valType v=Value; XMap_insert_base(this_map,&k,&v);}
bool XMapBase_insert_move_base(XMapBase* this_map,void* key,void* pvValue);
#define XMapBase_Insert_Move_Base(this_map,keyType,key,valType,Value) {keyType k=key;valType v=Value; XMapBase_insert_move_base(this_map,&k,&v);}
bool XMapBase_insert_keyMove_base(XMapBase* this_map,void* key, const void* pvValue);
bool XMapBase_insert_valueMove_base(XMapBase* this_map, const void* key,void* pvValue);

void XMapBase_erase_base(XMapBase* this_map, const XMapBase_iterator* it, XMapBase_iterator* next);
//map删除数据
bool XMapBase_remove_base(XMapBase* this_map, const void* key);
#define XMapBase_Remove_Base(this_map,keyType,key) {keyType k=key;XMapBase_remove_base(this_map,&k);}
//根据键值返回数据地址
void* XMapBase_value_base(XMapBase* this_map, const void* key);
#define XMapBase_Value_Base(this_map,key,valueType) (*(valueType*)XMapBase_value_base(this_map,&(key)))
//查找数据，返回找到的迭代器
bool XMapBase_find_base(const XMapBase* this_map, const void* key, XMapBase_iterator* it);
bool XMapBase_contains(const XMapBase* this_map, const void* key);
XVector* XMapBase_keys_base(const XMapBase* this_map);
XVector* XMapBase_values_base(const XMapBase* this_map);
#define XMapBase_copy_base				XContainerObject_copy_base	
#define XMapBase_move_base				XContainerObject_move_base	
#define XMapBase_deinit_base			XContainerObject_deinit_base	
#define XMapBase_delete_base			XContainerObject_delete_base	
#define XMapBase_clear_base				XContainerObject_clear_base	
#define XMapBase_isEmpty_base			XContainerObject_isEmpty_base	
#define XMapBase_size_base				XContainerObject_size_base	
#define XMapBase_capacity_base			XContainerObject_capacity_base
#define XMapBase_swap_base				XContainerObject_swap_base	
#define XMapBase_typeSize_base			XContainerObject_typeSize_base


#define XMapBaseKeyCopyMethod(Map) (((XMapBase*)(Map))->m_keyCopyMethod)//获取容器键拷贝方法
#define XMapBaseSetKeyCopyMethod(Map,method) (((XMapBase*)(Map))->m_keyCopyMethod=method)//设置容器的键拷贝方法
#define XMapBaseKeyMoveMethod(Map) (((XMapBase*)(Map))->m_keyMoveMethod)//获取容器键移动方法
#define XMapBaseSetKeyMoveMethod(Map,method) (((XMapBase*)(Map))->m_keyMoveMethod=method)//设置容器的键移动方法
#define XMapBaseKeyDeinitMethod(Map) (((XMapBase*)(Map))->m_keyDeinitMethod)//获取容器键释放方法
#define XMapBaseSetKeyDeinitMethod(Map,method) (((XMapBase*)(Map))->m_keyDeinitMethod=method)//设置容器的键释放方法

//字典映射的节点释放方法
void XMapBase_deleteNodeData(XPair** pair, XMapBase* this_map);
#ifdef __cplusplus
}
#endif
#endif// !XMap_H
