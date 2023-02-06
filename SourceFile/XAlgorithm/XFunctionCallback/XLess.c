#include "XLess.h"

const bool XLess_int(const void* LPrevValue, const void* LNextValue)
{
	return *(int*)LPrevValue < *(int*)LNextValue;
}
