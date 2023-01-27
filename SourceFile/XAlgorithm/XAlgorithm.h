#ifndef XALGORITHM_H
#define XALGORITHM_H
#include"XSort/XSort.h"
#include"XStringOperation.h"
struct XStack;
struct XVector;
#ifdef _WIN32
//控制台移动
void gotoxy(short x, short y);
#endif
//交换任意数据类型的函数
void swap(void* valOne, void* valTwo, const int typeSize);
//栈逆序拷贝至数组
void XStackRCopyXVector(const struct XStack* stack,struct XVector* vector);
//延迟毫秒
void XDelay(const size_t msec);
#endif // !XALGORITHM_H

