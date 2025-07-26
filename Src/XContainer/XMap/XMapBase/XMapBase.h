#include"XContainerObject.h"
#if !defined(XMAPBASE_H)&& XMap_ON
#define XMAPBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
#include"XEquality.h"
#include"XLess.h"
#include"XPair.h"
typedef struct XMapBase_iterator XMapBase_iterator;
#define XMAPBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XMapBase))       //XMap容器虚函数表大小
//XMap虚函数表枚举
XCLASS_DEFINE_BEGING(XMapBase)
XCLASS_DEFINE_ENUM(XMapBase,Insert_Copy)= XCLASS_VTABLE_GET_SIZE(XContainerObject),
XCLASS_DEFINE_ENUM(XMapBase,Insert_Move),
XCLASS_DEFINE_ENUM(XMapBase,Erase),
XCLASS_DEFINE_ENUM(XMapBase,Remove),
XCLASS_DEFINE_ENUM(XMapBase,Value),
XCLASS_DEFINE_ENUM(XMapBase,Find), 
XCLASS_DEFINE_ENUM(XMapBase,Keys),
XCLASS_DEFINE_END(XMapBase)
typedef struct XMapBase
{
	XContainerObject m_parent;//基本数据
	size_t m_keyTypeSize;//key数据类型大小
	XEquality m_KeyEquality;//key的相等比较函数
	XLess m_KeyLess;//key小于比较函数
}XMapBase;
XVtable* XMapBase_class_init();
//初始化 XMap
void XMapBase_init(XMapBase* this_map, const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality,XLess KeyLess);
//Map插入数据
bool XMapBase_insert_base(XMapBase* this_map, const void* pvKey, const void* pvValue);
#define XMapBase_Insert_Base(this_map,keyType,key,valType,Value) {keyType k=key;valType v=Value; XMap_insert_base(this_map,&k,&v);}
bool XMapBase_insert_move_base(XMapBase* this_map, const void* pvKey, const void* pvValue);
#define XMapBase_Insert_Move_Base(this_map,keyType,key,valType,Value) {keyType k=key;valType v=Value; XMapBase_insert_move_base(this_map,&k,&v);}

void XMapBase_erase_base(XMapBase* this_map, const XPair* pPair);
//map删除数据
bool XMapBase_remove_base(XMapBase* this_map, const void* pvKey);
#define XMapBase_Remove_Base(this_map,keyType,key) {keyType k=key;XMap_remove_base(this_map,&k);}
//根据键值返回数据地址
void* XMapBase_value_base(XMapBase* this_map, const void* pvKey);
#define XMapBase_Value_Base(this_map,key,ValueType) (*(ValueType*)XMap_value_base(this_map,&(key)))
//查找数据，返回找到的XPair地址，没有返回NULL
XPair* XMapBase_find_base(XMapBase* this_map, const void* pvKey);
bool XMapBase_contains(XMapBase* this_map, const void* pvKey);
XVector* XMapBase_keys_base(const XMapBase* this_map);
#define XMapBase_copy_base				XContainerObject_copy_base	
#define XMapBase_move_base				XContainerObject_move_base	
#define XMapBase_deinit_base			XContainerObject_deinit_base	
#define XMapBase_delete_base			XContainerObject_delete_base	
#define XMapBase_clear_base				XContainerObject_clear_base	
#define XMapBase_isEmpty_base			XContainerObject_isEmpty_base	
#define XMapBase_getSize_base			XContainerObject_getSize_base	
#define XMapBase_getCapacity_base		XContainerObject_getCapacity_base
#define XMapBase_swap_base				XContainerObject_swap_base	
#define XMapBase_getTypeSize_base		XContainerObject_getTypeSize_base


//默认释放派生类的方法 key是派生的容器
void XMapBase_KeyClassDeinitMethod(XPair* pair);
//默认释放派生类的方法 value是派生的容器
void XMapBase_ValueClassDeinitMethod(XPair* pair);
//默认释放XVariant value的方法
void XMapBase_ValueXVariantDeleteMethod(XPair* pair);
//默认释放派生类的方法 key和value都是派生的容器
void XMapBase_ClassDeinitMethod(XPair* pair);

void XMapBase_XVariantMapCopyMethod(XPair* pair, const XPair* src);
void XMapBase_XVariantMapMoveMethod(XPair* pair, XPair* src);
//XVariantMap释放
void XMapBase_XVariantMapDeinitMethod(XPair* pair);
#ifdef __cplusplus
}
#endif
#endif// !XMap_H
