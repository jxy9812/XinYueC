//相等回调函数
#ifndef XEQUALITY_H
#define XEQUALITY_H
#ifdef __cplusplus
extern "C" {
#endif
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
XDefineFunc_Define(XEquality, size_t)
XDefineFunc_Define(XEquality,ptr)
XDefineFunc_Define(XEquality, float)
XDefineFunc_Define(XEquality, double)
XDefineFunc_DefineTwo(XEquality, long, double)
#ifdef __cplusplus
}
#endif
//const bool XEquality_int(const void* LPrevValue, const void* LNextValue);
#endif // !XFUNCTIONPOINTER_H