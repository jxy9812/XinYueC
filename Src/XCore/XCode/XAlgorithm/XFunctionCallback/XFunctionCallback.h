//回调函数
#ifndef XFUNCTIONCALLBACK_H
#define XFUNCTIONCALLBACK_H

/**
 * @file XFunctionCallback.h
 * @brief 容器遍历、哈希和比较规则回调 API。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "XCompare.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 判断前一个值是否小于后一个值的回调类型。
 * @param LPrevValue 待比较的前一个值。
 * @param pvNextValue 待比较的后一个值。
 * @return 前一个值小于后一个值返回 true，否则返回 false。
 */
typedef bool (*XLess)(const void* LPrevValue, const void* pvNextValue);

/**
 * @brief 判断前一个值是否大于后一个值的回调类型。
 * @param LPrevValue 待比较的前一个值。
 * @param pvNextValue 待比较的后一个值。
 * @return 前一个值大于后一个值返回 true，否则返回 false。
 */
typedef bool (*XGreater)(const void* LPrevValue, const void* pvNextValue);

/**
 * @brief 判断两个值是否相等的回调类型。
 * @param pvValue 待比较的值。
 * @param pvCompareValue 目标比较值。
 * @return 两个值相等返回 true，否则返回 false。
 */
typedef bool (*XEquality)(const void* pvValue, const void* pvCompareValue);

/**
 * @brief 容器遍历回调类型。
 * @param pvValue 当前元素地址。
 * @param args 调用者传入的上下文参数，可为 NULL。
 */
typedef void (*XFor_each)(void* pvValue, void* args);

/**
 * @brief 无状态哈希函数回调类型。
 * @param key 待哈希数据地址；len 为 0 时可为 NULL。
 * @param len 数据长度，单位为字节。
 * @return 64 位哈希值。
 */
typedef uint64_t (*XHashFunc)(const void* key, size_t len);

/**
 * @brief 对两个普通对象直接调用 compare。
 * @param compare 基础比较器。
 * @param pvPrevValue 第一个对象。
 * @param pvNextValue 第二个对象。
 * @return compare 的结果。
 */
int32_t XCompareRuleTwo_Standard(XCompare compare,
    const void* pvPrevValue, const void* pvNextValue);

/**
 * @brief 二叉树使用的双对象比较规则。
 * @param compare 基础比较器。
 * @param pvPrevValue 第一个节点数据。
 * @param pvNextValue 第二个节点数据。
 * @return compare 的结果。
 */
int32_t XCompareRuleTwo_BinaryTree(XCompare compare,
    const void* pvPrevValue, const void* pvNextValue);

/**
 * @brief XMap 使用的双对象比较规则，比较两个键值对的 key。
 * @param compare 基础比较器。
 * @param pvPrevValue 第一个 XPair 对象地址。
 * @param pvNextValue 第二个 XPair 对象地址。
 * @return 两个 key 的比较结果。
 */
int32_t XCompareRuleTwo_XMap(XCompare compare,
    const void* pvPrevValue, const void* pvNextValue);

/**
 * @brief XSet 使用的双对象比较规则。
 * @param compare 基础比较器。
 * @param pvPrevValue 第一个集合元素。
 * @param pvNextValue 第二个集合元素。
 * @return compare 的结果。
 */
int32_t XCompareRuleTwo_XSet(XCompare compare,
    const void* pvPrevValue, const void* pvNextValue);

/**
 * @brief 对普通对象执行单对象查找比较。
 * @param compare 基础比较器。
 * @param Value 当前对象。
 * @param CompareValue 待比较对象。
 * @return compare 的结果。
 */
int32_t XCompareRuleOne_Standard(XCompare compare,
    const void* Value, const void* CompareValue);

/**
 * @brief 二叉树使用的单对象查找比较规则。
 * @param compare 基础比较器。
 * @param Value 当前节点数据。
 * @param CompareValue 待查找数据。
 * @return compare 的结果。
 */
int32_t XCompareRuleOne_BinaryTree(XCompare compare,
    const void* Value, const void* CompareValue);

/**
 * @brief XMap 使用的单对象查找比较规则，比较键值对的 key。
 * @param compare 基础比较器。
 * @param Value 当前 XPair 对象地址。
 * @param CompareValue 待查找的 key。
 * @return key 的比较结果。
 */
int32_t XCompareRuleOne_XMap(XCompare compare,
    const void* Value, const void* CompareValue);

/**
 * @brief XSet 使用的单对象查找比较规则。
 * @param compare 基础比较器。
 * @param Value 当前集合元素。
 * @param CompareValue 待查找元素。
 * @return compare 的结果。
 */
int32_t XCompareRuleOne_XSet(XCompare compare,
    const void* Value, const void* CompareValue);
#ifdef __cplusplus
}
#endif
#endif /* XFUNCTIONCALLBACK_H */
