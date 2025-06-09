#include"XContainerObject.h"
#if !defined(XMAP_H)&& XMap_ON
#define XMAP_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XPair.h"
#include"XMap_Iterator.h"
#include"XMap_reverse_iterator.h"
typedef struct XVector XVector;
typedef struct XPair XPair;
#define XMAP_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XContainerObject)+5)       //XMap容器虚函数表大小
//XMap虚函数表枚举
enum XMapEnum
{
	EXMap_Insert=EXContainerObject_Clear+1,
	EXMap_Erase,
	EXMap_Remove,
	EXMap_Value,
	EXMap_Find,
};
typedef struct XMap
{
	XContainerObject m_parent;//基本数据
	size_t m_keyTypeSize;//第二组数据类型大小
	XEquality m_KeyEquality;//key的相等比较函数
	XLess m_KeyLess;//key小于比较函数
	bool m_isModify;//是否修改了
	XVector* m_itArray;//迭代器数组
}XMap;
XVtable* XMap_class_init();
//开辟一个Map,初始化
XMap* XMap_create(const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess/*, XEquality ValEquality*/);
#define XMap_Create(keyType,valType,KeyEquality,KeyLess) XMap_create(sizeof(keyType),sizeof(valType),KeyEquality,KeyLess)
//初始化 XMap
void XMap_init(XMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess);
//Map插入数据
void XMap_insert_base(XMap* this_map, const void* key, const void* LpValue);
#define XMap_Insert_Base(this_map,keyType,key,valType,Value) {keyType k=key;valType v=Value; XMap_insert_base(this_map,&k,&v);}
void XMap_erase_base(XMap* this_map, const XPair** LPpair);
//map删除数据
void XMap_remove_base(XMap* this_map, const void* key);
#define XMap_Remove_Base(this_map,keyType,key) {keyType k=key;XMap_remove_base(this_map,&k);}
//根据键值返回数据地址
void* XMap_value_base(XMap* this_map, const void* key);
#define XMap_Value_Base(this_map,key,ValueType) (*(ValueType*)XMap_value_base(this_map,&(key)))
//查找数据，返回找到的XPair地址，没有返回NULL
XPair* XMap_find_base(XMap* this_map, const void* key);
//释放内存
#define XMap_delete_base				XContainerObject_delete_base
//清空，不是释放内存
#define XMap_clear_base				XContainerObject_clear_base
//检测是否为空，空为真 O(1)
#define XMap_isEmpty_base			XContainerObject_isEmpty_base
//返回元素的个数 O(1)
#define XMap_getSize_base				XContainerObject_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XMap_getCapacity_base			XContainerObject_getCapacity_base
//交换两个同类型向量的数据
#define XMap_swap_base				XContainerObject_swap_base
//返回元素类型字节大小
#define XMap_getTypeSize_base		XContainerObject_getTypeSize_base
//默认释放派生类的方法 key是派生的容器
void XMap_DefaultDerivedClassDataKeyDeleteMethod(void* args);
//默认释放派生类的方法 value是派生的容器
void XMap_DefaultDerivedClassDataValueDeleteMethod(void* args);
//默认释放派生类的方法 key和value都是派生的容器
void XMap_DefaultDerivedClassDataKeyValueDeleteMethod(void* args);

//其他函数
//插入迭代器地址
//void XMap_insertIterator(XMap* this_map, XPair* LPdata);
////删除迭代器地址
//void XMap_eraseIterator(XMap* this_map, XPair* LPdata);
////更新迭代器
void XMap_updataIterator(XMap* this_map);
#ifdef __cplusplus
}
#endif
#endif// !XMap_H
