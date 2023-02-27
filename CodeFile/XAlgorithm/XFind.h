#ifndef XFIND_H
#define XFIND_H
#include"XFunctionCallback.h"
#include<stdio.h>
//二分查找(必须时数组有序)
void* XBinarySearch(void* LPvalue,size_t n,size_t TypeSize, XLess less,XEquality equality, void* findVal);
#endif