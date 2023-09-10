#include"XFunctionCallback.h"
#include"XBinaryTreeObject.h"
#include"XPair.h"
//小于回调函数的标准准则
bool XCompareRuleTwo_Standard(XCompare compare, const void* LPrevValue, const void* LNextValue)
{
	return compare(LPrevValue, LNextValue);
}

bool XCompareRuleTwo_BinaryTree(XCompare compare, const void* LPrevValue, const void* LNextValue)
{
	return compare(XVector_at(((XBTreeNode*)LPrevValue)->values,0), XVector_at(((XBTreeNode*)LNextValue)->values, 0));
}

bool XCompareRuleTwo_XMap(XCompare compare, const void* LPrevValue, const void* LNextValue)
{
	return compare(XPair_first(*(XPair**)XVector_at(((XBTreeNode*)LPrevValue)->values,0)), XPair_first(*(XPair**)XVector_at(((XBTreeNode*)LNextValue)->values, 0)));
}

bool XCompareRuleOne_Standard(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(Value, CompareValue);
}

bool XCompareRuleOne_BinaryTree(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(XVector_at(((XBTreeNode*)Value)->values,0), CompareValue);
}

bool XCompareRuleOne_XMap(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(XPair_first(*(XPair**)(XVector_at(((XBTreeNode*)Value)->values, 0))), CompareValue);
}
