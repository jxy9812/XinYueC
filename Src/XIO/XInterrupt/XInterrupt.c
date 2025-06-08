#include"XInterrupt.h"
#include"XListSLinked.h"
#include"XEquality.h"
typedef struct XInterrupt
{
	InterruptCallback callback;
	void* userData;
}XInterrupt;//中断回调结构体
#define USART_COUNT 6  //串口数量
#define TIM_COUNT 14   //定时器数量
#if XINTERRUPT_ENABLE_USARTX_HANDLING
static XInterrupt USARTx[USART_COUNT] = {0};//串口回调
void XInterrupt_setUSARTxCallback(uint8_t portNum, InterruptCallback callback,void* userData)
{
	if (portNum == 0 || portNum > USART_COUNT)
		return;
	USARTx[portNum - 1].callback = callback;
	USARTx[portNum - 1].userData = userData;
}

InterruptCallback XInterrupt_getUSARTxCallback(uint8_t portNum)
{
	if (portNum == 0 || portNum > USART_COUNT)
		return NULL;
	return USARTx[portNum - 1].callback;
}

#endif

#if XINTERRUPT_ENABLE_TIMX_HANDLING
static XListSLinked* TIMx[TIM_COUNT] = {0};//定时器回调
static const _Bool XEquality_XInterrupt(const void* LPrevValue, const void* LNextValue)
{
	return (((XInterrupt*)LPrevValue)->callback==((XInterrupt*)LNextValue)->callback)&& (((XInterrupt*)LPrevValue)->userData==((XInterrupt*)LNextValue)->userData);
}
void XInterrupt_addTIMxCallback(uint8_t portNum, InterruptCallback callback, void *userData)
{
	if (portNum == 0 || portNum > TIM_COUNT)
		return;
	if(TIMx[portNum - 1]==NULL)
	{
		TIMx[portNum - 1]=XListSLinked_Create(XInterrupt);
		TIMx[portNum - 1]->m_parent.m_equality=XEquality_XInterrupt;
	}
	XListSLinked*list=TIMx[portNum - 1];	
	XListSNode* node=XListSLinked_find_base(list,&callback);
	XInterrupt i={callback,userData};
	if(node==NULL)
	{//插入
		XListSLinked_push_back_base(list,&i);
	}
	else
	{//更新
		XListSNode_Data(node,XInterrupt)=i;
	}
	
	// TIMx[portNum - 1].callback = callback;
	// TIMx[portNum - 1].userData = userData;
}
void XInterrupt_removeTIMxCallback(uint8_t portNum, InterruptCallback callback,void* userData)
{
	if (portNum == 0 || portNum > TIM_COUNT)
		return;
	if(TIMx[portNum - 1]==NULL)
		return;
	XListSLinked*list=TIMx[portNum - 1];
	XInterrupt i={callback,userData};	
	XListSLinked_remove_base(list,&i);
}
size_t XInterrupt_getTIMxCallbackSize(uint8_t portNum)
{
    if (portNum == 0 || portNum > TIM_COUNT)
		return 0;
	if(TIMx[portNum - 1]==NULL)
		return 0;
	return XListBase_getSize_base(TIMx[portNum - 1]);
}
#endif
//标准库
#ifdef USE_STDPERIPH_DRIVER
//启用了F4系列
#ifdef STM32F40_41xxx
#include"stm32f4xx.h"

//接管串口
#if XINTERRUPT_ENABLE_USARTX_HANDLING
//中断接收处理函数
#define USARTX_IRQHandler(port)  void USART##port##_IRQHandler(void)\
{\
   USARTx[port-1].callback(USARTx[port-1].userData);\
}
//接管F4 的四个串口中断处理函数
#if XINTERRUPT_ENABLE_USART1_HANDLING
USARTX_IRQHandler(1)
#endif
#if XINTERRUPT_ENABLE_USART2_HANDLING
USARTX_IRQHandler(2)
#endif
#if XINTERRUPT_ENABLE_USART3_HANDLING
USARTX_IRQHandler(3)
#endif
#if XINTERRUPT_ENABLE_USART6_HANDLING
USARTX_IRQHandler(6)
#endif
#endif
//接管定时器
#if XINTERRUPT_ENABLE_TIMX_HANDLING
//遍历链表
static void TIMX_Handler(uint8_t port) 
{
	XListSLinked*list=TIMx[port-1];
	for_each_iterator(list,XListSLinked,it)
	{
		XInterrupt* i=XListSNode_DataPtr(it);
		i->callback(i->userData);
	}
}

#define TIMX_IRQHandler(port)\
void TIM##port##_IRQHandler(void)\
{\
	TIMX_Handler(port);\
}

#if XINTERRUPT_ENABLE_TIM1_HANDLING

#endif
#if XINTERRUPT_ENABLE_TIM2_HANDLING
TIMX_IRQHandler(2)
#endif
#if XINTERRUPT_ENABLE_TIM3_HANDLING
TIMX_IRQHandler(3)
#endif
#if XINTERRUPT_ENABLE_TIM4_HANDLING
TIMX_IRQHandler(4)
#endif
#if XINTERRUPT_ENABLE_TIM5_HANDLING
TIMX_IRQHandler(5)
#endif
#if XINTERRUPT_ENABLE_TIM6_HANDLING

#endif
#if XINTERRUPT_ENABLE_TIM7_HANDLING

#endif
#if XINTERRUPT_ENABLE_TIM8_HANDLING

#endif
#if XINTERRUPT_ENABLE_TIM9_HANDLING
void TIM1_BRK_TIM9_IRQHandler(void) 
{
	TIMX_Handler(9);
}
#endif
#if XINTERRUPT_ENABLE_TIM10_HANDLING
void TIM1_UP_TIM10_IRQHandler(void) 
{
	TIMX_Handler(10);
}
#endif
#if XINTERRUPT_ENABLE_TIM11_HANDLING
void TIM1_TRG_COM_TIM11_IRQHandler(void) 
{
	TIMX_Handler(11);
}
#endif
#if XINTERRUPT_ENABLE_TIM12_HANDLING
void TIM8_BRK_TIM12_IRQHandler(void) 
{
	TIMX_Handler(12);
}
#endif
#if XINTERRUPT_ENABLE_TIM13_HANDLING
void TIM8_UP_TIM13_IRQHandler(void) 
{
	TIMX_Handler(13);
}
#endif
#if XINTERRUPT_ENABLE_TIM14_HANDLING
void TIM8_TRG_COM_TIM14_IRQHandler(void) 
{
	TIMX_Handler(14);
}
#endif

#endif

#endif

#endif