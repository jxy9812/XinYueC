//回调函数
#ifndef XFUNCTIONCALLBACK_H
#define XFUNCTIONCALLBACK_H
#include<stdbool.h>
//比较大小函数指针-回调函数
typedef  const bool(*XCompare)(const void* LPrevValue, const void* LNextValue);

//比较大小的-回调函数自定义准则，第一个自定义
typedef  const bool(*XCompareRuleOne)(XCompare compare, const void* Value, const void* CompareValue);
//比较大小的-回调函数自定义准则,两个自定义
typedef  const bool(*XCompareRuleTwo)(XCompare compare, const void* LPrevValue, const void* LNextValue);

//小于-比较的-回调函数(LPrevValue<LNextValue为真)
typedef  const bool(*XLess)(const void* LPrevValue, const void* LNextValue);
//大于-比较的-回调函数(LPrevValue>LNextValue为真)
typedef  const bool(*XGreater)(const void* LPrevValue, const void* LNextValue);
//相等-比较的-回调函数(LPrevValue==LNextValue为真)
typedef  const bool(*XEquality)(const void* Value, const void* CompareValue);

//容器for_each(容器循环遍历)回调函数
typedef void (*XFor_each)(void* LPVal,void* args);


//比较大小回调函数的标准准则
bool XCompareRuleTwo_Standard(XCompare compare, const void* LPrevValue, const void* LNextValue);
//比较大小回调函数的二叉树准则
bool XCompareRuleTwo_BinaryTree(XCompare compare, const void* LPrevValue, const void* LNextValue);
//比较大小回调函数的XMap准则
bool XCompareRuleTwo_XMap(XCompare compare, const void* LPrevValue, const void* LNextValue);

bool XCompareRuleOne_Standard(XCompare compare, const void* Value, const void* CompareValue);
bool XCompareRuleOne_BinaryTree(XCompare compare, const void* Value, const void* CompareValue);
bool XCompareRuleOne_XMap(XCompare compare, const void* Value, const void* CompareValue);
#endif // !XFUNCTIONPOINTER_H
