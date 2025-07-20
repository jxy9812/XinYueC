#include"XFunctionCallback.h"
#include"XClass.h"
#include"XTreeObject.h"
#include"XPair.h"
//小于回调函数的标准准则
bool XCompareRuleTwo_Standard(XCompare compare, const void* pvPrevValue, const void* pvNextValue)
{
	return compare(pvPrevValue, pvNextValue);
}

bool XCompareRuleTwo_BinaryTree(XCompare compare, const void* pvPrevValue, const void* pvNextValue)
{
	return compare(pvPrevValue, pvNextValue);
}

bool XCompareRuleTwo_XMap(XCompare compare, const void* pvPrevValue, const void* pvNextValue)
{
	return compare(XPair_first(*(XPair**)(pvPrevValue)), XPair_first(*(XPair**)(pvNextValue)));
}

bool XCompareRuleTwo_XSet(XCompare compare, const void* pvPrevValue, const void* pvNextValue)
{
	return compare(pvPrevValue, pvNextValue);
}


bool XCompareRuleOne_Standard(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(Value, CompareValue);
}

bool XCompareRuleOne_BinaryTree(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(Value, CompareValue);
}

bool XCompareRuleOne_XMap(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(XPair_first(*(XPair**)(Value)), CompareValue);
}

bool XCompareRuleOne_XSet(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(Value, CompareValue);
}
