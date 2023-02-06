//回调函数
#ifndef XFUNCTIONCALLBACK_H
#define XFUNCTIONCALLBACK_H
#include<stdbool.h>
//比较大小函数指针-回调函数
typedef  const bool(*XCompare)(const void* LPrevValue, const void* LNextValue);
//小于-比较的-回调函数(LPrevValue<LNextValue为真)
typedef  const bool(*XLess)(const void* LPrevValue, const void* LNextValue);
//大于-比较的-回调函数(LPrevValue>LNextValue为真)
typedef  const bool(*XGreater)(const void* LPrevValue, const void* LNextValue);
//相等-比较的-回调函数(LPrevValue==LNextValue为真)
typedef  const bool(*XEquality)(const void* Value, const void* CompareValue);
//容器for_each(容器循环遍历)回调函数
typedef void (*XFor_each)(void* LPVal,void* args);
#endif // !XFUNCTIONPOINTER_H
