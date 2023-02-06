#ifndef XDEFINEFUNC_H
#define XDEFINEFUNC_H
//定义
#define XDefineFunc_Define(name,type) const bool name##_##type(const void* LPrevValue, const void* LNextValue);
#define XDefineFunc_DefineTwo(name,typeOne,typeTwo) const bool name##_##typeOne##_##typeTwo(const void* LPrevValue, const void* LNextValue);
#define XDefineFunc_ComeTrue(name,com,type) const bool name##_##type(const void* LPrevValue, const void* LNextValue){return *(type*)LPrevValue com *(type*)LNextValue;}
#define XDefineFunc_ComeTrueTwo(name,com,typeOne,typeTwo) const bool name##_##typeOne##_##typeTwo(const void* LPrevValue, const void* LNextValue){return *(typeOne typeTwo*)LPrevValue com *(typeOne typeTwo*)LNextValue;}
#endif