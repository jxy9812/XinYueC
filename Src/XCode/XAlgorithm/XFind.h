#ifndef XFIND_H
#define XFIND_H

/**
 * @file XFind.h
 * @brief 通用查找算法 API。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "XCompare.h"
#include <stddef.h>

/**
 * @brief 在已按 compare 排序的连续数组中执行二分查找。
 * @param value 数组首地址。
 * @param n 数组元素数量；必须大于 0。
 * @param TypeSize 单个元素的字节数；必须大于 0。
 * @param compare 元素比较器，返回 XCompare_* 结果。
 * @param findVal 待查找值的地址。
 * @return 找到时返回数组中匹配元素的地址，未找到或参数无效时返回 NULL。
 * @note 数组顺序必须与 compare 的结果一致，返回地址仍属于调用者。
 */
void* XBinarySearch(void* value, size_t n, size_t TypeSize,
    XCompare compare, void* findVal);

#ifdef __cplusplus
}
#endif

#endif /* XFIND_H */
