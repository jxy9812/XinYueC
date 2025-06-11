#ifndef XDEFINEFUNC_H
#define XDEFINEFUNC_H
#include"XPair.h"
//函数定义
#define XDefineFunc_Define(name,type) const bool name##_##type(const void* pvPrevValue, const void* pvNextValue);
#define XDefineFunc_DefineTwo(name,typeOne,typeTwo) const bool name##_##typeOne##_##typeTwo(const void* pvPrevValue, const void* pvNextValue);
//函数实现
#define XDefineFunc_ComeTrue(name,com,type) const bool name##_##type(const void* pvPrevValue, const void* pvNextValue){return *(type*)pvPrevValue com *(type*)pvNextValue;}
#define XDefineFunc_ComeTrueTwo(name,com,typeOne,typeTwo) const bool name##_##typeOne##_##typeTwo(const void* pvPrevValue, const void* pvNextValue){return *(typeOne typeTwo*)pvPrevValue com *(typeOne typeTwo*)pvNextValue;}
#endif