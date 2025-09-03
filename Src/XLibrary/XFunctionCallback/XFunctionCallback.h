//回调函数
#ifndef XFUNCTIONCALLBACK_H
#define XFUNCTIONCALLBACK_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XTypes.h"
#include"XCompare.h"

//小于-比较的-回调函数(LPrevValue<LNextValue为真)
typedef  const bool(*XLess)(const void* LPrevValue, const void* pvNextValue);
//大于-比较的-回调函数(LPrevValue>LNextValue为真)
typedef  const bool(*XGreater)(const void* LPrevValue, const void* pvNextValue);
//相等-比较的-回调函数(LPrevValue==LNextValue为真)
typedef  const bool(*XEquality)(const void* pvValue, const void* pvCompareValue);

//容器for_each(容器循环遍历)回调函数
typedef void (*XFor_each)(void* pvValue,void* args);
//hash函数
typedef size_t(*XHashFunc)(const void* key, size_t len);

//比较大小回调函数的标准准则
int32_t XCompareRuleTwo_Standard(XCompare compare, const void* pvPrevValue, const void* pvNextValue);
//比较大小回调函数的二叉树准则
int32_t XCompareRuleTwo_BinaryTree(XCompare compare, const void* pvPrevValue, const void* pvNextValue);
//比较大小回调函数的XMap准则
int32_t XCompareRuleTwo_XMap(XCompare compare, const void* pvPrevValue, const void* pvNextValue);
//比较大小回调函数的XSet准则
int32_t XCompareRuleTwo_XSet(XCompare compare, const void* pvPrevValue, const void* pvNextValue);

int32_t XCompareRuleOne_Standard(XCompare compare, const void* Value, const void* CompareValue);
int32_t XCompareRuleOne_BinaryTree(XCompare compare, const void* Value, const void* CompareValue);
int32_t XCompareRuleOne_XMap(XCompare compare, const void* Value, const void* CompareValue);
int32_t XCompareRuleOne_XSet(XCompare compare, const void* Value, const void* CompareValue);
#ifdef __cplusplus
}
#endif
#endif // !XFUNCTIONPOINTER_H
