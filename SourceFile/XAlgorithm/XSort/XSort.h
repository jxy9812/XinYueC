#ifndef XSORT_H
#define XSORT_H
#include<stdio.h>
#include<stdbool.h>
//直接插入排序gap变量控制一次间隔
void InsertSort_gap(void* LArray, const size_t  nSize, const size_t TypeSize, const size_t gap, bool(*Sort)(const void* LPrevValue, const void* LNextValue));
//直接插入排序
void XInsertSort(void* LArray, const size_t nSize, const size_t TypeSize, bool(*Sort)(const void* LPrevValue, const void* LNextValue));
//希尔排序
void XShellSort(void* LArray, const size_t nSize, const size_t TypeSize, bool(*Sort)(const void* LPrevValue, const void* LNextValue));
//直接选择排序
void XSelectSort(void* LArray, const size_t nSize, const size_t TypeSize, bool(*Sort)(const void* LPrevValue, const void* LNextValue));
//堆排序
void XHeapSort(void* LArray, const size_t nSize, const size_t TypeSize, bool(*Sort)(const void* LPrevValue, const void* LNextValue));
//冒泡排序
void XBubbleSort(void* LArray, const size_t nSize, const size_t TypeSize, bool(*Sort)(const void* LPrevValue, const void* LNextValue));
//快速排序
void XQuickSort(void* LArray, const size_t nSize, const size_t TypeSize, bool(*Sort)(const void* LPrevValue, const void* LNextValue));
//挖坑法栈模拟递归
void XQuicPitSort_Stack(void* LArray, const size_t nSize, const size_t TypeSize, bool(*Sort)(const void* LPrevValue, const void* LNextValue));
//归并排序
void XMergeSort(void* LArray, const size_t nSize, const size_t TypeSize, bool(*Sort)(const void* LPrevValue, const void* LNextValue));

#endif // !XSORT_H
