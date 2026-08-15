#ifndef XSORT_H
#define XSORT_H

/**
 * @file XSort.h
 * @brief 面向连续内存数组的通用排序和重排 API。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "XCompare.h"
#include <stddef.h>

/**
 * @brief 对数组执行指定步长的直接插入排序。
 * @param LParray 数组首地址；元素按连续内存排列。
 * @param nSize 数组元素数量。
 * @param TypeSize 单个元素的字节数，必须大于 0。
 * @param gap 插入排序步长，必须大于 0 且不应大于 nSize。
 * @param compare 元素比较器。
 * @param order 排序方向。
 */
void InsertSort_gap(void* LParray, size_t nSize, size_t TypeSize,
    size_t gap, XCompare compare, XSortOrder order);

/**
 * @brief 使用直接插入排序对数组原地排序。
 * @param LParray 数组首地址。
 * @param nSize 数组元素数量。
 * @param TypeSize 单个元素的字节数，必须大于 0。
 * @param compare 元素比较器。
 * @param order 排序方向。
 */
void XInsertSort(void* LParray, size_t nSize, size_t TypeSize,
    XCompare compare, XSortOrder order);

/**
 * @brief 使用希尔排序对数组原地排序。
 * @param LParray 数组首地址。
 * @param nSize 数组元素数量。
 * @param TypeSize 单个元素的字节数，必须大于 0。
 * @param compare 元素比较器。
 * @param order 排序方向。
 */
void XShellSort(void* LParray, size_t nSize, size_t TypeSize,
    XCompare compare, XSortOrder order);

/**
 * @brief 使用直接选择排序对数组原地排序。
 * @param LParray 数组首地址。
 * @param nSize 数组元素数量。
 * @param TypeSize 单个元素的字节数，必须大于 0。
 * @param compare 元素比较器。
 * @param order 排序方向。
 */
void XSelectSort(void* LParray, size_t nSize, size_t TypeSize,
    XCompare compare, XSortOrder order);

/**
 * @brief 将数组原地调整为堆结构。
 * @param LPArray 数组首地址。
 * @param nSize 数组元素数量；必须大于 0。
 * @param TypeSize 单个元素的字节数，必须大于 0。
 * @param compare 元素比较器。
 * @param order 按该方向建立堆。
 * @note 该函数只保证堆结构，不保证数组整体有序。
 */
void XCreateHeap(void* LPArray, size_t nSize, size_t TypeSize,
    XCompare compare, XSortOrder order);

/**
 * @brief 使用堆排序对数组原地排序。
 * @param LParray 数组首地址。
 * @param nSize 数组元素数量。
 * @param TypeSize 单个元素的字节数，必须大于 0。
 * @param compare 元素比较器。
 * @param order 排序方向。
 */
void XHeapSort(void* LParray, size_t nSize, size_t TypeSize,
    XCompare compare, XSortOrder order);

/**
 * @brief 使用冒泡排序对数组原地排序。
 * @param LParray 数组首地址。
 * @param nSize 数组元素数量。
 * @param TypeSize 单个元素的字节数，必须大于 0。
 * @param compare 元素比较器。
 * @param order 排序方向。
 */
void XBubbleSort(void* LParray, size_t nSize, size_t TypeSize,
    XCompare compare, XSortOrder order);

/**
 * @brief 使用递归挖坑法快速排序对数组原地排序。
 * @param LParray 数组首地址。
 * @param nSize 数组元素数量。
 * @param TypeSize 单个元素的字节数，必须大于 0。
 * @param compare 元素比较器。
 * @param order 排序方向。
 */
void XQuickSort(void* LParray, size_t nSize, size_t TypeSize,
    XCompare compare, XSortOrder order);

/**
 * @brief 使用显式栈模拟递归的挖坑法快速排序。
 * @param LParray 数组首地址。
 * @param nSize 数组元素数量。
 * @param TypeSize 单个元素的字节数，必须大于 0。
 * @param compare 元素比较器。
 * @param order 排序方向。
 * @note 该实现依赖 XStack_ON 配置；关闭时函数不执行排序。
 */
void XQuicPitSort_Stack(void* LParray, size_t nSize, size_t TypeSize,
    XCompare compare, XSortOrder order);

/**
 * @brief 使用归并排序对数组原地排序。
 * @param LParray 数组首地址。
 * @param nSize 数组元素数量。
 * @param TypeSize 单个元素的字节数，必须大于 0。
 * @param compare 元素比较器。
 * @param order 排序方向。
 * @note 排序过程中会申请与数组等大的临时缓冲区。
 */
void XMergeSort(void* LParray, size_t nSize, size_t TypeSize,
    XCompare compare, XSortOrder order);

/**
 * @brief 使用全局随机数发生器打乱数组元素顺序。
 * @param LParray 数组首地址。
 * @param nSize 数组元素数量。
 * @param TypeSize 单个元素的字节数，必须大于 0。
 */
void XDerangement(void* LParray, size_t nSize, size_t TypeSize);

/**
 * @brief 将数组元素原地逆序排列。
 * @param LParray 数组首地址。
 * @param nSize 数组元素数量。
 * @param TypeSize 单个元素的字节数，必须大于 0。
 */
void XReversed(void* LParray, size_t nSize, size_t TypeSize);

#ifdef __cplusplus
}
#endif

#endif /* XSORT_H */
