#ifndef  XMAP_FUNC_H
#define XMAP_FUNC_H
#include"XFunctionCallback.h"
typedef struct XPair XPair;
typedef struct XMap XMap;
//Map插入数据
void XMap_insert(XMap* this_map, const void* key, const void* val);
//map删除数据
void XMap_erase(XMap* this_map, const void* key);
//查找数据，返回找到的指针，没有返回NULL
XPair* XMap_find( XMap* this_map, const void* key);
//清空Map，释放内存
void XMap_clear(XMap* this_map);
//释放内存
void XMap_free(XMap* this_map);
//检测Map内是否为空，空为真 O(1)
bool XMap_empty(const  XMap* this_map);
//返回Map内元素的个数 O(1)
int XMap_size(const  XMap* this_map);
//交换两个同类型向量的数据
void XMap_swap(XMap* this_mapOne, XMap* this_mapTwo);
//开辟一个动态数组,初始化
XMap* XMap_init(const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess/*, XEquality ValEquality*/);
//更新迭代器
void XMap_updataIterator(XMap* this_map);
#endif // ! XMAP_FUNC_H
