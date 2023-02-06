#include "XEquality.h"
XDefineFunc_ComeTrue(XEquality,==, char)
XDefineFunc_ComeTrueTwo(XEquality, == , unsigned, char)
XDefineFunc_ComeTrue(XEquality, == , int)
XDefineFunc_ComeTrueTwo(XEquality, == , unsigned, int)
XDefineFunc_ComeTrue(XEquality, == , float)
XDefineFunc_ComeTrue(XEquality, == , double)
//const bool XEquality_int(const void* LPrevValue, const void* LNextValue)
//{
//	return *(int*)LPrevValue == *(int*)LNextValue;
//}
