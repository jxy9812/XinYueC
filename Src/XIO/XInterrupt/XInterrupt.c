#include"XInterrupt.h"
#include"XListSLinked.h"
#include"XEquality.h"
#include<string.h>
typedef struct XInterruptNode
{
	InterruptCallback callback;
	void* userData;
}XInterruptNode;//中断回调结构体

typedef struct  XInterrupt
{
	void** m_data;//XListSLinked* TIMx[TIM_COUNT] = {0};
	size_t m_size;
}XInterrupt;
//遍历链表处理回调
static void List_Handler(void** data, uint16_t index)
{
	XListSLinked* list = data[index];
	//for_each_iterator(list,XListSLinked,it)
	XListSNode* node = XContainerDataPtr(list);
	{
		XInterruptNode* i = XListSNode_DataPtr(node);
		i->callback(i->userData);
		node = node->next;
	}
}
static const _Bool XEquality_XInterrupt(const void* LPrevValue, const void* LNextValue)
{
	return (((XInterruptNode*)LPrevValue)->callback == ((XInterruptNode*)LNextValue)->callback) && (((XInterruptNode*)LPrevValue)->userData == ((XInterruptNode*)LNextValue)->userData);
}
void XInterrupt_init_stack(XInterrupt* interrupt, void** data, size_t size)
{
	if (interrupt == NULL || interrupt->m_data != NULL)
		return;//仅初始化一次  
	interrupt->m_data = data;
	interrupt->m_size = size;
	memset(data, 0, sizeof(void*) * size);
}
void XInterrupt_addCallback(XInterrupt* interrupt, uint8_t index, InterruptCallback callback, void* userData)
{
	if (interrupt == NULL || index >= interrupt->m_size)
		return;
	XListSLinked* list = NULL;
	if (interrupt->m_data[index] == NULL)
	{
		interrupt->m_data[index] = XListSLinked_Create(XInterruptNode);
		((XListSLinked*)interrupt->m_data[index])->m_parent.m_equality = XEquality_XInterrupt;
	}
	list = interrupt->m_data[index];
	XListSNode* node = XListSLinked_find_base(list, &callback);
	XInterruptNode i = { callback,userData };
	if (node == NULL)
	{//插入
		XListSLinked_push_back_base(list, &i);
	}
	else
	{//更新
		XListSNode_Data(node, XInterruptNode) = i;
	}
}
void XInterrupt_removeCallback(XInterrupt* interrupt, uint8_t index, InterruptCallback callback, void* userData)
{
	if (interrupt == NULL || index >= interrupt->m_size)
		return;
	XListSLinked* list = interrupt->m_data[index];
	if (list == NULL)
		return;
	XInterruptNode i = { callback,userData };
	XListSLinked_remove_base(list, &i);
}
size_t XInterrupt_getCallbackSize(XInterrupt* interrupt, uint8_t index)
{
	if (interrupt == NULL || index >= interrupt->m_size)
		return 0;
	if (interrupt->m_data[index] == NULL)
		return 0;
	return XListBase_size_base(interrupt->m_data[index]);
}
#define XInterrupt_Create(name,size)\
static XInterrupt name={0};static void*name##data[size];
#define XInterrupt_Init(name) XInterrupt_init_stack(&name,name##data,sizeof(name##data)/sizeof(name##data[0]))
#define USART_COUNT 6  //串口数量
#define TIM_COUNT 14   //定时器数量
#if XINTERRUPT_ENABLE_USARTX_HANDLING
XInterrupt_Create(USARTx, USART_COUNT)
void XInterrupt_addUSARTxCallback(uint8_t portNum, InterruptCallback callback, void* userData)
{
	XInterrupt_Init(USARTx);
	XInterrupt_addCallback(&USARTx, portNum - 1, callback, userData);
}

void XInterrupt_removeUSARTxCallback(uint8_t portNum, InterruptCallback callback, void* userData)
{
	XInterrupt_Init(USARTx);
	XInterrupt_removeCallback(&USARTx, portNum - 1, callback, userData);
}

size_t XInterrupt_getUSARTxCallbackSize(uint8_t portNum)
{
	XInterrupt_Init(USARTx);
	return XInterrupt_getCallbackSize(&USARTx, portNum - 1);
}

#endif

#if XINTERRUPT_ENABLE_TIMX_HANDLING
XInterrupt_Create(TIMx, TIM_COUNT)
void XInterrupt_addTIMxCallback(uint8_t portNum, InterruptCallback callback, void* userData)
{
	XInterrupt_Init(TIMx);
	XInterrupt_addCallback(&TIMx, portNum - 1, callback, userData);
}
void XInterrupt_removeTIMxCallback(uint8_t portNum, InterruptCallback callback, void* userData)
{
	XInterrupt_Init(TIMx);
	XInterrupt_removeCallback(&TIMx, portNum - 1, callback, userData);
}
size_t XInterrupt_getTIMxCallbackSize(uint8_t portNum)
{
	XInterrupt_Init(TIMx);
	return XInterrupt_getCallbackSize(&TIMx, portNum - 1);
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
	List_Handler(USARTxdata,port-1);\
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
#define TIMX_IRQHandler(port)\
void TIM##port##_IRQHandler(void)\
{\
	List_Handler(TIMxdata,port-1);\
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
	List_Handler(TIMxdata, 9 - 1);
}
#endif
#if XINTERRUPT_ENABLE_TIM10_HANDLING
void TIM1_UP_TIM10_IRQHandler(void)
{
	List_Handler(TIMxdata, 10 - 1);
}
#endif
#if XINTERRUPT_ENABLE_TIM11_HANDLING
void TIM1_TRG_COM_TIM11_IRQHandler(void)
{
	List_Handler(TIMxdata, 11 - 1);
}
#endif
#if XINTERRUPT_ENABLE_TIM12_HANDLING
void TIM8_BRK_TIM12_IRQHandler(void)
{
	List_Handler(TIMxdata, 12 - 1);
}
#endif
#if XINTERRUPT_ENABLE_TIM13_HANDLING
void TIM8_UP_TIM13_IRQHandler(void)
{
	List_Handler(TIMxdata, 13 - 1);
}
#endif
#if XINTERRUPT_ENABLE_TIM14_HANDLING
void TIM8_TRG_COM_TIM14_IRQHandler(void)
{
	List_Handler(TIMxdata, 14 - 1);
}
#endif

#endif

#endif

#endif