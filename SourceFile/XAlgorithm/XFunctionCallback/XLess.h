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

XDefineFunc_XMapDefine(XLess, char)
XDefineFunc_XMapDefineTwo(XLess, unsigned, char)
XDefineFunc_XMapDefine(XLess, int)
XDefineFunc_XMapDefine(XLess, long)
XDefineFunc_XMapDefineTwo(XLess, unsigned, int)
XDefineFunc_XMapDefineTwo(XLess, short, int)
XDefineFunc_XMapDefineTwo(XLess, long, int)
XDefineFunc_XMapDefineTwo(XLess, long, long)
XDefineFunc_XMapDefineTwo(XLess, unsigned, long)
XDefineFunc_XMapDefine(XLess, float)
XDefineFunc_XMapDefine(XLess, double)
XDefineFunc_XMapDefineTwo(XLess, long, double)

#endif // !XFUNCTIONPOINTER_H