//大于回调函数
#ifndef XGREATER_H
#define XGREATER_H
#include<stdbool.h>
#include"XDefineFunc.h"
XDefineFunc_Define(XGreater, char)
XDefineFunc_DefineTwo(XGreater, unsigned, char)
XDefineFunc_Define(XGreater, int)
XDefineFunc_DefineTwo(XGreater, unsigned, int)
XDefineFunc_Define(XGreater, float)
XDefineFunc_Define(XGreater, double)
#endif // !XFUNCTIONPOINTER_H