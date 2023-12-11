#ifndef XMAP_H
#define XMAP_H
#include"XContainerObject.h"
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
	XMap_Insert=EXContainerObject_Clear+1,
	XMap_Erase,
	XMap_Remove,
	XMap_At,
};
typedef struct XMap
{
	XContainerObject object;//基本数据
	size_t keyTypeSize;//第二组数据类型大小
	XEquality KeyEquality;//key的相等比较函数
	XLess KeyLess;//key小于比较函数
	bool isModify;//是否修改了
	XVector* itArray;//迭代器数组
}XMap;
//开辟一个Map,初始化
XMap* XMap_new(const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess/*, XEquality ValEquality*/);
#define XMap_New(keyType,valType,KeyEquality,KeyLess) XMap_new(sizeof(keyType),sizeof(valType),KeyEquality,KeyLess)
//Map插入数据
void XMap_insert(XMap* this_map, const void* key, const void* LpValue);
//map删除数据
void XMap_erase(XMap* this_map, const void* key);
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
int XMap_size(const  XMap* this_map);
//交换两个同类型Map的数据
void XMap_swap(XMap* this_mapOne, XMap* this_mapTwo);

#endif // !XMap_H
