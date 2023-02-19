#ifndef  XMAP_FUNC_H
#define XMAP_FUNC_H
#include"XFunctionCallback.h"
typedef struct XPair XPair;
typedef struct XMap XMap;

//插入迭代器地址
void XMap_insertIterator(XMap* this_map,XPair* LPdata);
//删除迭代器地址
void XMap_eraseIterator(XMap* this_map, XPair* LPdata);
//更新迭代器
void XMap_updataIterator(XMap* this_map);
#endif // ! XMAP_FUNC_H
