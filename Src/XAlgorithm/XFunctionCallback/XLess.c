#include "XLess.h"
#include "XString.h"
#include <string.h>
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
XDefineFunc_ComeTrue(XLess, <  , uint8_t)
XDefineFunc_ComeTrue(XLess, <  , uint16_t)
XDefineFunc_ComeTrue(XLess, <  , uint32_t)
XDefineFunc_ComeTrue(XLess, <  , uint64_t)
XDefineFunc_ComeTrue(XLess, <  , int8_t)
XDefineFunc_ComeTrue(XLess, <  , int16_t)
XDefineFunc_ComeTrue(XLess, <  , int32_t)
XDefineFunc_ComeTrue(XLess, <  , int64_t)
XDefineFunc_ComeTrue(XLess, <  , size_t)
const bool XLess_ptr(const void* pvPrevValue, const void* pvNextValue)
{
	return *(void**)pvPrevValue < *(void**)pvNextValue;
}
const bool XLess_XString(const void* pvPrevValue, const void* pvNextValue)
{
	if (pvPrevValue == NULL || pvNextValue == NULL)
		return false;
	if (XString_size_base(pvPrevValue) != XString_size_base(pvNextValue))
		return false;
	return strcmp(XContainerDataPtr(pvPrevValue), XContainerDataPtr(pvNextValue)) <0;
}