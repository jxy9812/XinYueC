//相等回调函数
#ifndef XEQUALITY_H
#define XEQUALITY_H
#include <stdbool.h>
#include"XDefineFunc.h"
XDefineFunc_Define(XEquality, char)
XDefineFunc_DefineTwo(XEquality, unsigned, char)
XDefineFunc_Define(XEquality, int)
XDefineFunc_Define(XEquality, long)
XDefineFunc_DefineTwo(XEquality, unsigned, int)
XDefineFunc_DefineTwo(XEquality, short, int)
XDefineFunc_DefineTwo(XEquality, long, int)
XDefineFunc_DefineTwo(XEquality, long, long)
XDefineFunc_DefineTwo(XEquality, unsigned, long)
XDefineFunc_Define(XEquality, float)
XDefineFunc_Define(XEquality, double)
XDefineFunc_DefineTwo(XEquality, long, double)

XDefineFunc_XMapDefine(XEquality, char)
XDefineFunc_XMapDefineTwo(XEquality, unsigned, char)
XDefineFunc_XMapDefine(XEquality, int)
XDefineFunc_XMapDefine(XEquality, long)
XDefineFunc_XMapDefineTwo(XEquality, unsigned, int)
XDefineFunc_XMapDefineTwo(XEquality, short, int)
XDefineFunc_XMapDefineTwo(XEquality, long, int)
XDefineFunc_XMapDefineTwo(XEquality, long, long)
XDefineFunc_XMapDefineTwo(XEquality, unsigned, long)
XDefineFunc_XMapDefine(XEquality, float)
XDefineFunc_XMapDefine(XEquality, double)
XDefineFunc_XMapDefineTwo(XEquality, long, double)
//const bool XEquality_int(const void* LPrevValue, const void* LNextValue);
#endif // !XFUNCTIONPOINTER_H