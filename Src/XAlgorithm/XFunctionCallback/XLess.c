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

const bool XLess_XString(const void* pvPrevValue, const void* pvNextValue)
{
	if (pvPrevValue == NULL || pvNextValue == NULL)
		return false;
	if (XString_getSize_base(*((XString**)pvPrevValue)) != XString_getSize_base(*((XString**)pvPrevValue)))
		return false;
	return strcmp(XContainerDataPtr(*((XString**)pvPrevValue)), XContainerDataPtr(*((XString**)pvPrevValue))) <0;
}