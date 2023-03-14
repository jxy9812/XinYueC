#ifndef XMAP_MACRO_H
#define XMAP_MACRO_H
//初始化创建一个map
#define XMap_Init(keyType,valType,KeyEquality,KeyLess) XMap_init(sizeof(keyType),sizeof(valType),KeyEquality,KeyLess)
//根据键获取数据
#define XMap_At(this_map,key,ValueType) (*(ValueType*)XMap_at(this_map,&key))
#endif

