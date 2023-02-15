//大于回调函数
#ifndef XGREATER_H
#define XGREATER_H
#include<stdbool.h>
#include"XDefineFunc.h"
XDefineFunc_Define(XGreater, char)
XDefineFunc_DefineTwo(XGreater, unsigned, char)
XDefineFunc_Define(XGreater, int)
XDefineFunc_Define(XGreater, long)
XDefineFunc_DefineTwo(XGreater, unsigned, int)
XDefineFunc_DefineTwo(XGreater, short, int)
XDefineFunc_DefineTwo(XGreater, long, int)
XDefineFunc_DefineTwo(XGreater, long, long)
XDefineFunc_DefineTwo(XGreater, unsigned, long)
XDefineFunc_Define(XGreater, float)
XDefineFunc_Define(XGreater, double)
XDefineFunc_DefineTwo(XGreater, long, double)

XDefineFunc_XMapDefine(XGreater, char)
XDefineFunc_XMapDefineTwo(XGreater, unsigned, char)
XDefineFunc_XMapDefine(XGreater, int)
XDefineFunc_XMapDefine(XGreater, long)
XDefineFunc_XMapDefineTwo(XGreater, unsigned, int)
XDefineFunc_XMapDefineTwo(XGreater, short, int)
XDefineFunc_XMapDefineTwo(XGreater, long, int)
XDefineFunc_XMapDefineTwo(XGreater, long, long)
XDefineFunc_XMapDefineTwo(XGreater, unsigned, long)
XDefineFunc_XMapDefine(XGreater, float)
XDefineFunc_XMapDefine(XGreater, double)
XDefineFunc_XMapDefineTwo(XGreater, long, double)
#endif // !XFUNCTIONPOINTER_H