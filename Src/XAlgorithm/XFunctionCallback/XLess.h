//小于回调函数
#ifndef XLESS_H
#define XLESS_H
#include<stdbool.h>
#include"XDefineFunc.h"

XDefineFunc_Define(XLess, char)
XDefineFunc_DefineTwo(XLess, unsigned, char)
XDefineFunc_Define(XLess, int)
XDefineFunc_Define(XLess, long)
XDefineFunc_DefineTwo(XLess, unsigned, int)
XDefineFunc_DefineTwo(XLess, short, int)
XDefineFunc_DefineTwo(XLess, long, int)
XDefineFunc_DefineTwo(XLess, long, long)
XDefineFunc_DefineTwo(XLess, unsigned, long)
XDefineFunc_Define(XLess, float)
XDefineFunc_Define(XLess, double)
XDefineFunc_DefineTwo(XLess, long, double)
XDefineFunc_Define(XLess, uint8_t)
XDefineFunc_Define(XLess, uint16_t)
XDefineFunc_Define(XLess, uint32_t)
XDefineFunc_Define(XLess, uint64_t)
XDefineFunc_Define(XLess, int8_t)
XDefineFunc_Define(XLess, int16_t)
XDefineFunc_Define(XLess, int32_t)
XDefineFunc_Define(XLess, int64_t)
XDefineFunc_Define(XLess, size_t)
XDefineFunc_Define(XLess, ptr)//void*

XDefineFunc_Define(XLess, XString)
#endif // !XFUNCTIONPOINTER_H