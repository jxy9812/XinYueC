#include "XLess.h"

XDefineFunc_ComeTrue(XLess, < , char)
XDefineFunc_ComeTrueTwo(XLess, < , unsigned, char)
XDefineFunc_ComeTrue(XLess, < , int)
XDefineFunc_ComeTrue(XLess, < , long)
XDefineFunc_ComeTrueTwo(XLess, <  , unsigned, int)
XDefineFunc_ComeTrueTwo(XLess, <  , short, int)
XDefineFunc_ComeTrueTwo(XLess, <  , long, int)
XDefineFunc_ComeTrueTwo(XLess, <  , long, long)
XDefineFunc_ComeTrueTwo(XLess, <  , unsigned, long)
XDefineFunc_ComeTrue(XLess, <  , float)
XDefineFunc_ComeTrue(XLess, <  , double)
XDefineFunc_ComeTrueTwo(XLess, < , long, double)

//const bool XLess_int(const void* LPrevValue, const void* LNextValue)
//{
//	return *(int*)LPrevValue < *(int*)LNextValue;
//}
