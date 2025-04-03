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
//XMap虚函数表
extern XVtable* XMapVtable;
//XMap虚函数表枚举
enum XMapEnum
{
	EXMap_Insert=EXContainerObject_Clear+1,
	EXMap_Erase,
	EXMap_Remove,
	EXMap_At,
	EXMap_Find,
};
typedef struct XMap
{
	XContainerObject m_object;//基本数据
	size_t m_keyTypeSize;//第二组数据类型大小
	XEquality m_KeyEquality;//key的相等比较函数
	XLess m_KeyLess;//key小于比较函数
	bool m_isModify;//是否修改了
	XVector* m_itArray;//迭代器数组
}XMap;
//开辟一个Map,初始化
XMap* XMap_new(const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess/*, XEquality ValEquality*/);
#define XMap_New(keyType,valType,KeyEquality,KeyLess) XMap_new(sizeof(keyType),sizeof(valType),KeyEquality,KeyLess)
//初始化类
void XMap_class_init();
//初始化 XMap
void XMap_init(XMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess);
//Map插入数据
void XMap_insert(XMap* this_map, const void* key, const void* LpValue);
#define XMap_Insert(this_map,keyType,key,valType,Value) {keyType k=key;valType v=Value; XMap_insert(this_map,&k,&v);}
void XMap_erase(XMap* this_map, const XPair** LPpair);
//map删除数据
void XMap_remove(XMap* this_map, const void* key);
#define XMap_Remove(this_map,keyType,key) {keyType k=key;XMap_remove(this_map,&k);}
//根据键值返回数据地址
void* XMap_at(XMap* this_map, const void* key);
#define XMap_At(this_map,key,ValueType) (*(ValueType*)XMap_at(this_map,&(key)))
//查找数据，返回找到的XPair地址，没有返回NULL
XPair* XMap_find(XMap* this_map, const void* key);
//清空Map，释放内存
void XMap_clear(XMap* this_map);
//释放内存
void XMap_free(XMap* this_map);
//检测Map内是否为空，空为真 O(1)
bool XMap_empty(const  XMap* this_map);
//返回Map内元素的个数 O(1)
size_t XMap_size(const  XMap* this_map);
//交换两个同类型Map的数据
void XMap_swap(XMap* this_mapOne, XMap* this_mapTwo);

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
