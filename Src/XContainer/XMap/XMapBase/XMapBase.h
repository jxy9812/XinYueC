#include"XContainerObject.h"
#if !defined(XMAPBASE_H)&& XMap_ON
#define XMAPBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
#include"XPair.h"
#define XMAPBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XMapBase))       //XMap容器虚函数表大小
//XMap虚函数表枚举
XCLASS_DEFINE_BEGING(XMapBase)
XCLASS_DEFINE_ENUM(XMapBase,Insert)= XCLASS_VTABLE_GET_SIZE(XContainerObject),
XCLASS_DEFINE_ENUM(XMapBase,Erase),
XCLASS_DEFINE_ENUM(XMapBase,Remove),
XCLASS_DEFINE_ENUM(XMapBase,Value),
XCLASS_DEFINE_ENUM(XMapBase,Find), 
XCLASS_DEFINE_END(XMapBase)
typedef struct XMapBase
{
	XContainerObject m_parent;//基本数据
	size_t m_keyTypeSize;//key数据类型大小
	XEquality m_KeyEquality;//key的相等比较函数
}XMapBase;
XVtable* XMapBase_class_init();
//初始化 XMap
void XMapBase_init(XMapBase* this_map, const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality);
//Map插入数据
bool XMapBase_insert_base(XMapBase* this_map, const void* pvKey, const void* pvValue);
#define XMapBase_Insert_Base(this_map,keyType,key,valType,Value) {keyType k=key;valType v=Value; XMap_insert_base(this_map,&k,&v);}
void XMapBase_erase_base(XMapBase* this_map, const XPair* pPair);
//map删除数据
bool XMapBase_remove_base(XMapBase* this_map, const void* pvKey);
#define XMapBase_Remove_Base(this_map,keyType,key) {keyType k=key;XMap_remove_base(this_map,&k);}
//根据键值返回数据地址
void* XMapBase_value_base(XMapBase* this_map, const void* pvKey);
#define XMapBase_Value_Base(this_map,key,ValueType) (*(ValueType*)XMap_value_base(this_map,&(key)))
//查找数据，返回找到的XPair地址，没有返回NULL
XPair* XMapBase_find_base(XMapBase* this_map, const void* pvKey);
//释放内存
#define XMapBase_delete_base					XContainerObject_delete_base
//清空，不是释放内存
#define XMapBase_clear_base						XContainerObject_clear_base
//检测是否为空，空为真 O(1)
#define XMapBase_isEmpty_base					XContainerObject_isEmpty_base
//返回元素的个数 O(1)
#define XMapBase_getSize_base					XContainerObject_getSize_base
//返回当前Map所能容纳的最大元素个数
#define XMapBase_getCapacity_base				XContainerObject_getCapacity_base
//交换两个同类型Map的数据
#define XMapBase_swap_base						XContainerObject_swap_base
//返回元素类型字节大小
#define XMapBase_getTypeSize_base				XContainerObject_getTypeSize_base
//默认释放派生类的方法 key是派生的容器
void XMapBase_KeyDeleteMethod(void* args);
//默认释放派生类的方法 value是派生的容器
void XMapBase_ValueDeleteMethod(void* args);
//默认释放派生类的方法 key和value都是派生的容器
void XMapBase_KeyValueDeleteMethod(void* args);

#ifdef __cplusplus
}
#endif
#endif// !XMap_H
