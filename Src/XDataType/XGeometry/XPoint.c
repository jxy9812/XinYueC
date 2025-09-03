#include"XPoint.h"
#include"string.h"
#include"XCompare.h"

int32_t XPoint_compare(const XPoint* lhs, const XPoint* rhs)
{
	if(lhs->x==rhs->x&&lhs->y==rhs->y)
		return XCompare_Equality;
	if (lhs->x * lhs->y < rhs->x * rhs->y)
		return XCompare_Less;
	return XCompare_Greater;
}