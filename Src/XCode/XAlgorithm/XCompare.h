//比较大小函数指针-回调函数
#ifndef XCOMPARE_H
#define XCOMPARE_H

/**
 * @file XCompare.h
 * @brief 通用比较回调、排序顺序和内置标量比较器。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "XTypes.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief 排序结果的方向。 */
typedef enum XSortOrder {
    XSORT_ASC,  /**< 升序，从小到大。 */
    XSORT_DESC  /**< 降序，从大到小。 */
} XSortOrder;

#define XCompare_Less     (-1)        /**< lhs 小于 rhs。 */
#define XCompare_Greater  (1)         /**< lhs 大于 rhs。 */
#define XCompare_Equality (0)         /**< lhs 等于 rhs。 */
#define XCompare_Other    (INT32_MAX) /**< 无法比较或比较器返回其他结果。 */

/**
 * @brief 比较两个对象的回调函数类型。
 * @param lhs 左操作数。
 * @param rhs 右操作数。
 * @return 应返回 XCompare_Less、XCompare_Equality 或 XCompare_Greater。
 */
typedef int32_t (*XCompare)(const void* lhs, const void* rhs);

/**
 * @brief 使用一个待比较值的查找/树比较规则回调类型。
 * @param compare 基础对象比较器。
 * @param pvValue 当前对象或节点数据。
 * @param pvCompareValue 用户要查找或比较的值。
 * @return 应返回基础比较器的结果，或按容器规则转换后的结果。
 */
typedef int32_t (*XCompareRuleOne)(XCompare compare,
    const void* pvValue, const void* pvCompareValue);

/**
 * @brief 使用两个对象的排序/插入比较规则回调类型。
 * @param compare 基础对象比较器。
 * @param lhs 第一个对象。
 * @param rhs 第二个对象。
 * @return 应返回基础比较器的结果，或按容器规则转换后的结果。
 */
typedef int32_t (*XCompareRuleTwo)(XCompare compare,
    const void* lhs, const void* rhs);

/**
 * @brief 生成指定类型的比较函数声明。
 * @param type 参与比较的 C 类型；该类型必须支持 < 和 > 运算。
 * @note 宏展开后函数名为 type_compare。
 */
#define XCompare_Define(type) \
    int32_t type##_compare(const type* lhs, const type* rhs)

/**
 * @brief 生成带有两个类型标识的比较函数声明。
 * @param typeOne 比较对象的基础类型限定符或类型名。
 * @param typeTwo 用于拼接函数名的第二个标识，同时参与生成参数类型。
 * @note 宏展开后函数名为 typeOne_typeTwo_compare。
 */
#define XCompare_DefineTwo(typeOne, typeTwo) \
    int32_t typeOne##_##typeTwo##_compare( \
        const typeOne typeTwo* lhs, const typeOne typeTwo* rhs)

/**
 * @brief 生成常规标量比较函数的定义。
 * @param type 参与比较的 C 类型；该类型必须支持 < 和 > 运算。
 */
#define XCompare_Come(type) XCompare_Define(type){if((*lhs)<(*rhs))return XCompare_Less;else if((*lhs)>(*rhs))return XCompare_Greater;return XCompare_Equality;}

/**
 * @brief 生成带有两个类型标识的常规标量比较函数定义。
 * @param typeOne 比较对象的基础类型限定符或类型名。
 * @param typeTwo 用于拼接函数名的第二个标识，同时参与生成参数类型。
 */
#define XCompare_ComeTwo(typeOne,typeTwo) XCompare_DefineTwo(typeOne,typeTwo){if((*lhs)<(*rhs))return XCompare_Less;else if((*lhs)>(*rhs))return XCompare_Greater;return XCompare_Equality;}

/** @brief 比较两个 bool 值。 */
XCompare_Define(bool);
/** @brief 比较两个 unsigned char 值。 */
XCompare_DefineTwo(unsigned,char);
/** @brief 比较两个 char 值。 */
XCompare_Define(char);
/** @brief 比较两个 int 值。 */
XCompare_Define(int);
/** @brief 比较两个 long 值。 */
XCompare_Define(long);
/** @brief 比较两个 float 值。 */
XCompare_Define(float);
/** @brief 比较两个 double 值。 */
XCompare_Define(double);
/** @brief 比较两个 uint8_t 值。 */
XCompare_Define(uint8_t);
/** @brief 比较两个 uint16_t 值。 */
XCompare_Define(uint16_t);
/** @brief 比较两个 uint32_t 值。 */
XCompare_Define(uint32_t);
/** @brief 比较两个 uint64_t 值。 */
XCompare_Define(uint64_t);
/** @brief 比较两个 int8_t 值。 */
XCompare_Define(int8_t);
/** @brief 比较两个 int16_t 值。 */
XCompare_Define(int16_t);
/** @brief 比较两个 int32_t 值。 */
XCompare_Define(int32_t);
/** @brief 比较两个 int64_t 值。 */
XCompare_Define(int64_t);
/** @brief 比较两个 size_t 值。 */
XCompare_Define(size_t);
/** @brief 比较两个 uintptr_t 值。 */
XCompare_Define(uintptr_t);

#ifdef __cplusplus
}
#endif

#endif /* XCOMPARE_H */
