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

XDefineFunc_Define(XLess, XString)
#endif // !XFUNCTIONPOINTER_H