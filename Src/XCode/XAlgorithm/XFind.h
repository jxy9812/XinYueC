#ifndef XFIND_H
#define XFIND_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XCompare.h"
#include<stdio.h>
//二分查找(必须时数组有序)
void* XBinarySearch(void* value,size_t n,size_t TypeSize, XCompare compare, void* findVal);
#ifdef __cplusplus
}
#endif
#endif