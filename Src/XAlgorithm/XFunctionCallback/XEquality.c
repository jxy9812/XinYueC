#include "XEquality.h"
#include "XPair.h"
#include "XPoint.h"
#include "XByteArray.h"
#include <string.h>
XDefineFunc_ComeTrue(XEquality, == , bool)
XDefineFunc_ComeTrue(XEquality, == , char)
XDefineFunc_ComeTrueTwo(XEquality, == , unsigned, char)
XDefineFunc_ComeTrue(XEquality, == , int)
XDefineFunc_ComeTrue(XEquality, == , long)
XDefineFunc_ComeTrueTwo(XEquality, == , unsigned, int)
XDefineFunc_ComeTrueTwo(XEquality, == , short, int)
XDefineFunc_ComeTrueTwo(XEquality, == , long, int)
XDefineFunc_ComeTrueTwo(XEquality, == , long, long)
XDefineFunc_ComeTrueTwo(XEquality, == , unsigned, long)
XDefineFunc_ComeTrue(XEquality, == , uint8_t)
XDefineFunc_ComeTrue(XEquality, == , uint16_t)
XDefineFunc_ComeTrue(XEquality, == , uint32_t)
XDefineFunc_ComeTrue(XEquality, == , uint64_t)
XDefineFunc_ComeTrue(XEquality, == , int8_t)
XDefineFunc_ComeTrue(XEquality, == , int16_t)
XDefineFunc_ComeTrue(XEquality, == , int32_t)
XDefineFunc_ComeTrue(XEquality, == , int64_t)
XDefineFunc_ComeTrue(XEquality, == , size_t)
XDefineFunc_ComeTrue(XEquality, == , float)
XDefineFunc_ComeTrue(XEquality, == , double)
XDefineFunc_ComeTrueTwo(XEquality, == , long, double)
const bool XEquality_ptr(const void* pvPrevValue, const void* pvNextValue)
{
	return *(void**)pvPrevValue == *(void**)pvNextValue;
}
const bool XEquality_c_str(const void* pvPrevValue, const void* pvNextValue)
{
	return strcmp(pvPrevValue, pvNextValue)==0;
}
const bool XEquality_XPair(const void* pvPrevValue, const void* pvNextValue)
{
	if (XPair_getSize(pvPrevValue) != XPair_getSize(pvNextValue))
		return false;
	return memcmp(pvPrevValue, pvNextValue, XPair_getSize(pvPrevValue)) == 0;
}
const bool XEquality_XPoint(const void* pvPrevValue, const void* pvNextValue)
{
	return memcmp(pvPrevValue, pvNextValue,sizeof(XPoint))==0;
}
const bool XEquality_XByteArray(const void* pvPrevValue, const void* pvNextValue)
{
	if (XByteArray_size_base(pvPrevValue) != XByteArray_size_base(pvNextValue))
		return false;
	return memcmp(XContainerDataPtr(pvPrevValue), XContainerDataPtr(pvNextValue), XByteArray_size_base(pvPrevValue)) == 0;
}
const bool XEquality_XString(const void* pvPrevValue, const void* pvNextValue)
{
	return XEquality_XByteArray(pvPrevValue,pvNextValue);
}