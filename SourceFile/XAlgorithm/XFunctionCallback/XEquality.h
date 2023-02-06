//相等回调函数
#ifndef XEQUALITY_H
#define XEQUALITY_H
#include <stdbool.h>
#include"XDefineFunc.h"
XDefineFunc_Define(XEquality,char)
XDefineFunc_DefineTwo(XEquality,unsigned, char)
XDefineFunc_Define(XEquality, int)
XDefineFunc_DefineTwo(XEquality, unsigned, int)
XDefineFunc_Define(XEquality, float)
XDefineFunc_Define(XEquality, double)
//const bool XEquality_int(const void* LPrevValue, const void* LNextValue);
#endif // !XFUNCTIONPOINTER_H