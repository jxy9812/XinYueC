#include "XFunctionCallbackEquality.h"

const bool XEquality_int(const void* LPrevValue, const void* LNextValue)
{
	return *(int*)LPrevValue == *(int*)LNextValue;
}
