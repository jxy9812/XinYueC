//小于回调函数
#ifndef XLESS_H
#define XLESS_H
#include<stdbool.h>
#include"XDefineFunc.h"
XDefineFunc_Define(XLess, char)
XDefineFunc_DefineTwo(XLess, unsigned, char)
XDefineFunc_Define(XLess, int)
XDefineFunc_DefineTwo(XLess, unsigned, int)
XDefineFunc_Define(XLess, float)
XDefineFunc_Define(XLess, double)
#endif // !XFUNCTIONPOINTER_H