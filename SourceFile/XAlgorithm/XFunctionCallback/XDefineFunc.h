#ifndef XDEFINEFUNC_H
#define XDEFINEFUNC_H
#include"XPair.h"
//函数定义
#define XDefineFunc_Define(name,type) const bool name##_##type(const void* LPrevValue, const void* LNextValue);
#define XDefineFunc_XMapDefine(name,type) const bool name##_##type##_##XMap(const void* LPrevValue, const void* LNextValue);
#define XDefineFunc_DefineTwo(name,typeOne,typeTwo) const bool name##_##typeOne##_##typeTwo(const void* LPrevValue, const void* LNextValue);
#define XDefineFunc_XMapDefineTwo(name,typeOne,typeTwo) const bool name##_##typeOne##_##typeTwo##_##XMap(const void* LPrevValue, const void* LNextValue);
//函数实现
#define XDefineFunc_ComeTrue(name,com,type) const bool name##_##type(const void* LPrevValue, const void* LNextValue){return *(type*)LPrevValue com *(type*)LNextValue;}
#define XDefineFunc_XMapComeTrue(name,com,type) const bool name##_##type##_##XMap(const void* LPrevValue, const void* LNextValue){return XPair_First(*(XPair**)LPrevValue,type) com XPair_First(*(XPair**)LNextValue,type);}
#define XDefineFunc_ComeTrueTwo(name,com,typeOne,typeTwo) const bool name##_##typeOne##_##typeTwo(const void* LPrevValue, const void* LNextValue){return *(typeOne typeTwo*)LPrevValue com *(typeOne typeTwo*)LNextValue;}
#define XDefineFunc_XMapComeTrueTwo(name,com,typeOne,typeTwo) const bool name##_##typeOne##_##typeTwo##_##XMap(const void* LPrevValue, const void* LNextValue){return XPair_First(*(XPair**)LPrevValue,typeOne typeTwo) com XPair_First(*(XPair**)LNextValue,typeOne typeTwo);}

#endif