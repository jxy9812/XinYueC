//比较大小函数指针-回调函数
#ifndef XCOMPARE_H
#define XCOMPARE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTypes.h"
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
// 定义排序顺序的枚举
typedef enum  
{
    XSORT_ASC,  // 升序（从小到大）
    XSORT_DESC  // 降序（从大到小）
}XSortOrder;

#define XCompare_Less				-1 //小于
#define XCompare_Greater			1//大于
#define XCompare_Equality			0//相等
#define XCompare_Other				INT32_MAX//其他(无法比较)
typedef void* ptr;
//比较大小函数指针-回调函数
typedef  int32_t(*XCompare)(const void* lhs, const void* rhs);
//比较大小的-回调函数自定义准则，第一个自定义
typedef  int32_t(*XCompareRuleOne)(XCompare compare, const void* pvValue, const void* pvCompareValue);
//比较大小的-回调函数自定义准则,两个自定义
typedef  int32_t(*XCompareRuleTwo)(XCompare compare, const void* lhs, const void* rhs);
//函数定义
#define XCompare_Define(type) int32_t XCompare_##type(const type* lhs, const type* rhs)
#define XCompare_DefineTwo(typeOne,typeTwo) int32_t XCompare_##typeOne##_##typeTwo(const typeOne typeTwo* lhs, const typeOne typeTwo* rhs)
//函数实现
#define XCompare_Come(type) int32_t XCompare_##type(const type* lhs, const type* rhs){if((*lhs)<(*rhs))return XCompare_Less;else if((*lhs)>(*rhs))return XCompare_Greater;return XCompare_Equality;}
#define XCompare_ComeTwo(typeOne,typeTwo) int32_t XCompare_##typeOne##_##typeTwo(const typeOne typeTwo* lhs, const typeOne typeTwo* rhs){if((*lhs)<(*rhs))return XCompare_Less;else if((*lhs)>(*rhs))return XCompare_Greater;return XCompare_Equality;}
XCompare_Define(bool);
XCompare_DefineTwo(unsigned,char);
XCompare_Define(char);
XCompare_Define(int);
XCompare_Define(long);
XCompare_Define(float);
XCompare_Define(double);
XCompare_Define(uint8_t);
XCompare_Define(uint16_t);
XCompare_Define(uint32_t);
XCompare_Define(uint64_t);
XCompare_Define(int8_t);
XCompare_Define(int16_t);
XCompare_Define(int32_t);
XCompare_Define(int64_t);
XCompare_Define(size_t);
XCompare_Define(ptr);

#ifdef __cplusplus
}
#endif
#endif // !XFUNCTIONPOINTER_H