#include"XFunctionCallback.h"
#include"XClass.h"
#include"XTreeObject.h"
#include"XPair.h"
//小于回调函数的标准准则
int32_t XCompareRuleTwo_Standard(XCompare compare, const void* pvPrevValue, const void* pvNextValue)
{
	return compare(pvPrevValue, pvNextValue);
}

int32_t XCompareRuleTwo_BinaryTree(XCompare compare, const void* pvPrevValue, const void* pvNextValue)
{
	return compare(pvPrevValue, pvNextValue);
}

int32_t XCompareRuleTwo_XMap(XCompare compare, const void* pvPrevValue, const void* pvNextValue)
{
	return compare(XPair_first((XPair*)(pvPrevValue)), XPair_first((XPair*)(pvNextValue)));
}

int32_t XCompareRuleTwo_XSet(XCompare compare, const void* pvPrevValue, const void* pvNextValue)
{
	return compare(pvPrevValue, pvNextValue);
}


int32_t XCompareRuleOne_Standard(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(Value, CompareValue);
}

int32_t XCompareRuleOne_BinaryTree(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(Value, CompareValue);
}

int32_t XCompareRuleOne_XMap(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(XPair_first((XPair*)(Value)), CompareValue);
}

int32_t XCompareRuleOne_XSet(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(Value, CompareValue);
}
