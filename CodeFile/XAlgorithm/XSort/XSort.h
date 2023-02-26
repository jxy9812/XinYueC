#ifndef XSORT_H
#define XSORT_H
#include<stdio.h>
#include<stdbool.h>
#include"XFunctionCallback.h"
//直接插入排序gap变量控制一次间隔
void InsertSort_gap(void* LParray, const size_t  nSize, const size_t TypeSize, const size_t gap,XCompare compare );
//直接插入排序
void XInsertSort(void* LParray, const size_t nSize, const size_t TypeSize,XCompare compare );
//希尔排序
void XShellSort(void* LParray, const size_t nSize, const size_t TypeSize,XCompare compare );
//直接选择排序
void XSelectSort(void* LParray, const size_t nSize, const size_t TypeSize,XCompare compare );
//创建堆(调整数组使其成为堆结构)
void XCreateHeap(void* LPArray, const size_t nSize, const size_t TypeSize, XCompare compare);
//堆排序
void XHeapSort(void* LParray, const size_t nSize, const size_t TypeSize,XCompare compare );
//冒泡排序
void XBubbleSort(void* LParray, const size_t nSize, const size_t TypeSize,XCompare compare );
//快速排序
void XQuickSort(void* LParray, const size_t nSize, const size_t TypeSize,XCompare compare );
//挖坑法栈模拟递归
void XQuicPitSort_Stack(void* LParray, const size_t nSize, const size_t TypeSize,XCompare compare );
//归并排序
void XMergeSort(void* LParray, const size_t nSize, const size_t TypeSize,XCompare compare );
//乱序(打乱原有顺序)
void XDerangement(void* LParray, const size_t nSize, const size_t TypeSize);
#endif // !XSORT_H
