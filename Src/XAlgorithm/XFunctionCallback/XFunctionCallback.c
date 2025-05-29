#include"XFunctionCallback.h"
#include"XClass.h"
#include"XBinaryTreeObject.h"
#include"XPair.h"
//小于回调函数的标准准则
bool XCompareRuleTwo_Standard(XCompare compare, const void* LPrevValue, const void* LNextValue)
{
	return compare(LPrevValue, LNextValue);
}

bool XCompareRuleTwo_BinaryTree(XCompare compare, const void* LPrevValue, const void* LNextValue)
{
#if XVector_ON
	return compare(XVector_at_base(((XBTreeNode*)LPrevValue)->values,0), XVector_at_base(((XBTreeNode*)LNextValue)->values, 0));
#else
	IS_ON_DEBUG(XVector_ON);
	return false;
#endif
}

bool XCompareRuleTwo_XMap(XCompare compare, const void* LPrevValue, const void* LNextValue)
{
#if XVector_ON
	return compare(XPair_first(*(XPair**)XVector_at_base(((XBTreeNode*)LPrevValue)->values,0)), XPair_first(*(XPair**)XVector_at_base(((XBTreeNode*)LNextValue)->values, 0)));
#else
	IS_ON_DEBUG(XVector_ON);
	return false;
#endif
}

bool XCompareRuleOne_Standard(XCompare compare, const void* Value, const void* CompareValue)
{
	return compare(Value, CompareValue);
}

bool XCompareRuleOne_BinaryTree(XCompare compare, const void* Value, const void* CompareValue)
{
#if XVector_ON
	return compare(XVector_at_base(((XBTreeNode*)Value)->values,0), CompareValue);
#else
	IS_ON_DEBUG(XVector_ON);
	return false;
#endif
}

bool XCompareRuleOne_XMap(XCompare compare, const void* Value, const void* CompareValue)
{
#if XVector_ON
	return compare(XPair_first(*(XPair**)(XVector_at_base(((XBTreeNode*)Value)->values, 0))), CompareValue);
#else
	IS_ON_DEBUG(XVector_ON);
	return false;
#endif
}
