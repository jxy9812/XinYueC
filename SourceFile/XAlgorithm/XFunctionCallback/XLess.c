#include "XLess.h"
XDefineFunc_ComeTrue(XLess,<, char)
XDefineFunc_ComeTrueTwo(XLess, < , unsigned, char)
XDefineFunc_ComeTrue(XLess, < , int)
XDefineFunc_ComeTrueTwo(XLess, < , unsigned, int)
XDefineFunc_ComeTrue(XLess, < , float)
XDefineFunc_ComeTrue(XLess, < , double)
//const bool XLess_int(const void* LPrevValue, const void* LNextValue)
//{
//	return *(int*)LPrevValue < *(int*)LNextValue;
//}
