#ifndef XINTERRUPT_H
#define XINTERRUPT_H
#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
typedef void (*InterruptCallback)(void* data);//中断回调函数

//接管串口中断
#define  XINTERRUPT_ENABLE_USARTX_HANDLING			(1)
#define  XINTERRUPT_ENABLE_USART1_HANDLING			(1)
#define  XINTERRUPT_ENABLE_USART2_HANDLING			(1)
#define  XINTERRUPT_ENABLE_USART3_HANDLING			(1)
#define  XINTERRUPT_ENABLE_USART4_HANDLING			(0)
#define  XINTERRUPT_ENABLE_USART5_HANDLING			(0)
#define  XINTERRUPT_ENABLE_USART6_HANDLING			(1)
#define  XINTERRUPT_ENABLE_USART7_HANDLING			(0)
#define  XINTERRUPT_ENABLE_USART8_HANDLING			(0)

#if XINTERRUPT_ENABLE_USARTX_HANDLING
//中断设置串口回调
void XInterrupt_setUSARTxCallback(uint8_t portNum, InterruptCallback callback,void* userData);
InterruptCallback  XInterrupt_getUSARTxCallback(uint8_t portNum);
#endif // 0

//接管定时器中断
#define  XINTERRUPT_ENABLE_TIMX_HANDLING			(1)
#define  XINTERRUPT_ENABLE_TIM1_HANDLING			(1)
#define  XINTERRUPT_ENABLE_TIM2_HANDLING			(1)
#define  XINTERRUPT_ENABLE_TIM3_HANDLING			(0)
#define  XINTERRUPT_ENABLE_TIM4_HANDLING			(0)
#define  XINTERRUPT_ENABLE_TIM5_HANDLING			(0)
#define  XINTERRUPT_ENABLE_TIM6_HANDLING			(0)
#define  XINTERRUPT_ENABLE_TIM7_HANDLING			(0)
#define  XINTERRUPT_ENABLE_TIM8_HANDLING			(0)
#define  XINTERRUPT_ENABLE_TIM9_HANDLING			(0)
#define  XINTERRUPT_ENABLE_TIM10_HANDLING			(0)
#define  XINTERRUPT_ENABLE_TIM11_HANDLING			(0)
#define  XINTERRUPT_ENABLE_TIM12_HANDLING			(0)
#define  XINTERRUPT_ENABLE_TIM13_HANDLING			(0)
#define  XINTERRUPT_ENABLE_TIM14_HANDLING			(1)
#if XINTERRUPT_ENABLE_TIMX_HANDLING
//中断设置定时器回调
void XInterrupt_addTIMxCallback(uint8_t portNum, InterruptCallback callback,void* userData);
void XInterrupt_removeTIMxCallback(uint8_t portNum, InterruptCallback callback,void* userData);
size_t XInterrupt_getTIMxCallbackSize(uint8_t portNum);
#endif // 0

#endif // !XInterrupt_H
