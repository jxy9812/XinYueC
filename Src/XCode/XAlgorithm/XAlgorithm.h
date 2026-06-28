#ifndef XALGORITHM_H
#define XALGORITHM_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdint.h>
typedef struct XStack XStack;
typedef struct XVector XVector;
#ifdef _WIN32
//控制台移动
void gotoxy(short x, short y);
#endif
//交换任意数据类型的函数
#define XSWAP(ValOne,ValTwo) (XSwap(ValOne,ValTwo,sizeof(ValOne)))
//交换任意数据类型的函数
void XSwap(void* valOne, void* valTwo, const int typeSize);
//栈逆序拷贝至数组
void XStackRCopyXVector(const XStack* stack,XVector* vector);
//栈拷贝至数组
void XStackCopyXVector(const XStack* stack, XVector* vector);
//延迟毫秒
void XDelay(const size_t msec);

/*
* @brief  根据需要的模式大小端转化后写入
* @param  data:待转化的数据
* @param  mode:转化模式 1是转大端 0是转小端   
* @retval 转化后的数据 
*/
uint16_t SwapEndian16(uint16_t data, uint8_t mode);
#ifdef __cplusplus
}
#endif
#endif// !XALGORITHM_H

