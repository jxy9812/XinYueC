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
	return compare(((XBTreeNode*)LPrevValue)->LPvalue, ((XBTreeNode*)LNextValue)->LPvalue);
}

bool XCompareRuleTwo_XMap(XCompare compare, const void* LPrevValue, const void* LNextValue)
{
	return compare(XPair_first(*(XPair**)(((XBTreeNode*)LPrevValue)->LPvalue)), XPair_first(*(XPair**)(((XBTreeNode*)LNextValue)->LPvalue)));
}

bool XCompareRuleOne_Standard(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(Value, CompareValue);
}

bool XCompareRuleOne_BinaryTree(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(((XBTreeNode*)Value)->LPvalue, CompareValue);
}

bool XCompareRuleOne_XMap(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(XPair_first(*(XPair**)(((XBTreeNode*)Value)->LPvalue)), CompareValue);
}
