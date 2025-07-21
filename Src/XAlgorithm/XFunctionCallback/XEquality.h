//相等回调函数
#ifndef XEQUALITY_H
#define XEQUALITY_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include"XDefineFunc.h"
XDefineFunc_Define(XEquality, bool)
XDefineFunc_Define(XEquality, char)
XDefineFunc_DefineTwo(XEquality, unsigned, char)
XDefineFunc_Define(XEquality, int)
XDefineFunc_Define(XEquality, long)
XDefineFunc_DefineTwo(XEquality, unsigned, int)
XDefineFunc_DefineTwo(XEquality, short, int)
XDefineFunc_DefineTwo(XEquality, long, int)
XDefineFunc_DefineTwo(XEquality, long, long)
XDefineFunc_DefineTwo(XEquality, unsigned, long)
XDefineFunc_Define(XEquality, uint8_t)
XDefineFunc_Define(XEquality, uint16_t)
XDefineFunc_Define(XEquality, uint32_t)
XDefineFunc_Define(XEquality, uint64_t)
XDefineFunc_Define(XEquality, int8_t)
XDefineFunc_Define(XEquality, int16_t)
XDefineFunc_Define(XEquality, int32_t)
XDefineFunc_Define(XEquality, int64_t)
XDefineFunc_Define(XEquality, size_t)
XDefineFunc_Define(XEquality,ptr)//void*
XDefineFunc_Define(XEquality, float)
XDefineFunc_Define(XEquality, double)
XDefineFunc_DefineTwo(XEquality, long, double)
//其他
XDefineFunc_Define(XEquality,c_str)//
XDefineFunc_Define(XEquality,XPair)//XPair
XDefineFunc_Define(XEquality,XPoint)//XPoint
XDefineFunc_Define(XEquality,XByteArray)//XByteArray*
XDefineFunc_Define(XEquality,XString)//XString*
#ifdef __cplusplus
}
#endif
//const bool XEquality_int(const void* LPrevValue, const void* LNextValue);
#endif // !XFUNCTIONPOINTER_H