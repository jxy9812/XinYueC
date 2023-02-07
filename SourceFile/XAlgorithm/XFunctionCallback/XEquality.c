#include "XEquality.h"
XDefineFunc_ComeTrue(XEquality, == , char)
XDefineFunc_ComeTrueTwo(XEquality, == , unsigned, char)
XDefineFunc_ComeTrue(XEquality, == , int)
XDefineFunc_ComeTrue(XEquality, == , long)
XDefineFunc_ComeTrueTwo(XEquality, == , unsigned, int)
XDefineFunc_ComeTrueTwo(XEquality, == , short, int)
XDefineFunc_ComeTrueTwo(XEquality, == , long, int)
XDefineFunc_ComeTrueTwo(XEquality, == , long, long)
XDefineFunc_ComeTrueTwo(XEquality, == , unsigned, long)
XDefineFunc_ComeTrue(XEquality, == , float)
XDefineFunc_ComeTrue(XEquality, == , double)
XDefineFunc_ComeTrueTwo(XEquality, == , long, double)
//const bool XEquality_int(const void* LPrevValue, const void* LNextValue)
//{
//	return *(int*)LPrevValue == *(int*)LNextValue;
//}
