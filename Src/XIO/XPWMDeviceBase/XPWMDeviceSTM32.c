#ifdef USE_STDPERIPH_DRIVER
#include"XPWMDeviceSTM32.h"
#include"XInterrupt.h"
#include"XMemory.h"
static void VXPWMDevice_setFrequency(XPWMDeviceSTM32* pwm, size_t f);
static void VXPWMDevice_setDutyCycle(XPWMDeviceSTM32* pwm, uint8_t d);
static void VXPWMDevice_start(XPWMDeviceSTM32* pwm);
static void VXPWMDevice_stop(XPWMDeviceSTM32* pwm);
static bool VXIODevice_open(XPWMDeviceSTM32 *pwm, XIODeviceBaseMode mode);
static size_t VXIODevice_write(XPWMDeviceSTM32 *pwm, const char* data, size_t maxSize);//写入
static size_t VXIODevice_read(XPWMDeviceSTM32 *pwm, char* data, size_t maxSize);//读取
static void VXIODevice_close(XPWMDeviceSTM32 *pwm);
XVtable *XPWMDeviceSTM32_class_init()
{
   	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XPWMDEVICESTM32_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XPWMDeviceBase_class_init());
	// void* table[] = { 
	// 	VXPWMDevice_setFrequency,VXPWMDevice_setDutyCycle,
	// 	VXPWMDevice_start,VXPWMDevice_stop,
	// 	VXPWMDevice_isRunning,VXPWMDevice_getFrequency,
	// 	VXPWMDevice_getDutyCycle };
	// //追加虚函数
	// XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Open,VXIODevice_open);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Write,VXIODevice_write);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Read,VXIODevice_read);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Close,VXIODevice_close);
	XVTABLE_OVERLOAD_DEFAULT(EXPWMDeviceBase_Start,VXPWMDevice_start);
	XVTABLE_OVERLOAD_DEFAULT(EXPWMDeviceBase_Stop,VXPWMDevice_stop);
	XVTABLE_OVERLOAD_DEFAULT(EXPWMDeviceBase_SetFrequency,VXPWMDevice_setFrequency);
	XVTABLE_OVERLOAD_DEFAULT(EXPWMDeviceBase_SetDutyCycle,VXPWMDevice_setDutyCycle);
#if SHOWCONTAINERSIZE
	printf("XPWMDeviceSTM32 size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

XPWMDeviceSTM32 *XPWMDeviceSTM32_create(XPWMGPIO* gpio)
{
	XPWMDeviceSTM32 * pwm=XMemory_malloc(sizeof(XPWMDeviceSTM32));
	if(pwm==NULL)
		return NULL;
	XPWMDeviceSTM32_init(pwm);
	pwm->m_gpio=*gpio;
    return pwm;
}

 bool XPWMDeviceSTM32_open_base(XPWMDeviceSTM32 *pwm, XIODeviceBaseMode mode, uint8_t portNum, uint8_t oc, void (*updateCallback)(void *))
{
 	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
 		return false;
 	pwm->m_oc=oc;
 	pwm->m_portNum=portNum;
 	pwm->m_updateCallback=updateCallback;
    return XClassGetVirtualFunc(pwm, EXIODeviceBase_Open, bool(*)(XPWMDeviceSTM32*, XIODeviceBaseMode))(pwm, mode);
 }

void XPWMDeviceSTM32_init(XPWMDeviceSTM32 *pwm)
{
	if (pwm == NULL)
    	return;
	memset(((XPWMDeviceBase*)pwm) + 1, 0, sizeof(XPWMDeviceSTM32) - sizeof(XPWMDeviceBase));
	XPWMDeviceBase_init(pwm, NULL);
	XClassGetVtable(pwm) = XPWMDeviceSTM32_class_init();
}

#endif

#ifdef USE_STDPERIPH_DRIVER
typedef struct TIM
{
    uint8_t   GPIO_AF_TIMX;
    uint32_t  TIMX;
    uint32_t  TIMX_Clock;
}TIM;
//启用了F4系列
#ifdef STM32F40_41xxx
#include"stm32f4xx.h"
#define TIM_COUNT 14 //
#define  STM32F407_168M_APB1_1MHz_Prescaler (84-1)//STM32F407 主频168M步进电机定时器在1Mz的预分频系数
#define  STM32F407_168M_APB2_1MHz_Prescaler (168-1)//STM32F407 主频168M步进电机定时器在1Mz的预分频系数
static XPWMDeviceSTM32* openTIM[TIM_COUNT] = { 0 };
void VXPWMDevice_setFrequency(XPWMDeviceSTM32 *pwm, size_t f)
{
	pwm->m_parent.m_frequency = f;
	uint32_t value=pwm->m_countFrequency/(f);
	((TIM_TypeDef *) pwm->m_TIMX)->ARR=value-1;//设置溢出装载值
    //TIM9->ARR=value-1;//设置溢出装载值
    
}
void VXPWMDevice_setDutyCycle(XPWMDeviceSTM32 *pwm, uint8_t d)
{
	if (pwm != NULL && d >= 0 && d <= 100)
	{
		pwm->m_parent.m_dutyCycle=d;
		uint32_t value=pwm->m_countFrequency/(pwm->m_parent.m_frequency);
		switch (pwm->m_oc)
		{
			case 1:(((TIM_TypeDef *) pwm->m_TIMX)->CCR1)=(value)*((d)/100.0);break;
			case 2:(((TIM_TypeDef *) pwm->m_TIMX)->CCR2)=(value)*((d)/100.0);break;
			case 3:(((TIM_TypeDef *) pwm->m_TIMX)->CCR3)=(value)*((d)/100.0);break;
			case 4:(((TIM_TypeDef *) pwm->m_TIMX)->CCR4)=(value)*((d)/100.0);break;
		default:
			break;
		}
	}
}
void VXPWMDevice_start(XPWMDeviceSTM32 *pwm)
{
	if(!pwm->m_parent.m_isRun)
	{
		TIM_Cmd(pwm->m_TIMX, ENABLE);
		pwm->m_parent.m_isRun = true;
		if (pwm->m_parent.m_runChangeCallback)
		{
			if (XIODeviceBase_CallbackQueue(pwm))
				XIODeviceBase_callbackQueue_push(pwm, pwm->m_parent.m_runChangeCallback);
			else
				pwm->m_parent.m_runChangeCallback(pwm);
		}
	}
}
void VXPWMDevice_stop(XPWMDeviceSTM32 *pwm)
{
	if (pwm->m_parent.m_isRun)
	{//如果运行则执行关闭
		TIM_Cmd(pwm->m_TIMX, DISABLE);
		pwm->m_parent.m_isRun = false;
		if (pwm->m_parent.m_runChangeCallback)
		{
			if (XIODeviceBase_CallbackQueue(pwm))
				XIODeviceBase_callbackQueue_push(pwm, pwm->m_parent.m_runChangeCallback);
			else
				pwm->m_parent.m_runChangeCallback(pwm);
		}
	}
}
static void TIMCallback(XPWMDeviceSTM32 *pwm)
{
    if (TIM_GetITStatus(pwm->m_TIMX, TIM_IT_Update) != RESET) 
  	{
		if(pwm->m_updateCallback!=NULL)
			pwm->m_updateCallback(pwm->m_userData);
	  	TIM_ClearITPendingBit(pwm->m_TIMX, TIM_IT_Update);
    }
}
// 打开的定时器
bool VXIODevice_open(XPWMDeviceSTM32 *pwm, XIODeviceBaseMode mode)
{
	if (XPWMDeviceBase_isOpen(pwm))
     	return true;//已经打开了
 	uint8_t portIndex = pwm->m_portNum - 1;
 	if (openTIM[portIndex] != NULL || (!(mode & XIODeviceBase_ReadOnly | mode & XIODeviceBase_WriteOnly)))
    	return false;//已经被打开了打开失败
	TIM Tim1 = { GPIO_AF_TIM1,TIM1,RCC_APB2Periph_TIM1 };
	TIM Tim2 = { GPIO_AF_TIM2,TIM2,RCC_APB1Periph_TIM2 };
	TIM Tim3 = { GPIO_AF_TIM3,TIM3,RCC_APB1Periph_TIM3 };
	TIM Tim4 = { GPIO_AF_TIM4,TIM4,RCC_APB1Periph_TIM4 };
	TIM Tim5 = { GPIO_AF_TIM5,TIM5,RCC_APB1Periph_TIM5 };
	TIM Tim8 = { GPIO_AF_TIM8,TIM8,RCC_APB2Periph_TIM8 };
	TIM Tim9 = { GPIO_AF_TIM9,TIM9,RCC_APB2Periph_TIM9 };
	TIM Tim10 = { GPIO_AF_TIM10,TIM10,RCC_APB2Periph_TIM10 };
	TIM Tim11 = { GPIO_AF_TIM11,TIM11,RCC_APB2Periph_TIM11 };
	TIM Tim12 = { GPIO_AF_TIM12,TIM12,RCC_APB1Periph_TIM12 };
	TIM Tim13 = { GPIO_AF_TIM13,TIM13,RCC_APB1Periph_TIM13 };
	TIM Tim14 = { GPIO_AF_TIM14,TIM14,RCC_APB1Periph_TIM14 };
	TIM tim[] = { Tim1,Tim2,Tim3,Tim4,Tim5,{0},{0},Tim8,Tim9,Tim10,Tim11,Tim12,Tim13,Tim14};	
	// 定义定时器基本初始化结构体变量
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	// 定义定时器输出比较初始化结构体变量
	TIM_OCInitTypeDef TIM_OCInitStructure;
	// 定义 GPIO 初始化结构体变量
	GPIO_InitTypeDef GPIO_InitStructure;
	// 使能 GPIO 端口的时钟
	RCC_AHB1PeriphClockCmd(pwm->m_gpio.GPIO_Clock, ENABLE);
	// 使能 TIM1 定时器的时钟
	if (((pwm->m_portNum > 1) && (pwm->m_portNum < 8)) || pwm->m_portNum > 11)//APB1
		RCC_APB1PeriphClockCmd(tim[portIndex].TIMX_Clock, ENABLE);
	else
		RCC_APB2PeriphClockCmd(tim[portIndex].TIMX_Clock, ENABLE);
	// 将 GPIOF 的引脚 9 复用为 TIM14 的功能
	GPIO_PinAFConfig(pwm->m_gpio.GPIOX, pwm->m_gpio.GPIO_PinSourceX, tim[portIndex].GPIO_AF_TIMX);

	// 设置 GPIO 模式为复用输出模式
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	// 设置要配置的 GPIO 引脚为 PF11
	GPIO_InitStructure.GPIO_Pin =pwm->m_gpio.GPIO_Pin_X;
	// 设置 GPIO 引脚的速度为 100MHz
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	// 设置 GPIO 输出类型为推挽输出
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	// 设置 GPIO 引脚的上拉/下拉模式为上拉
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	// 根据上述配置初始化 GPIOF 端口
	GPIO_Init(pwm->m_gpio.GPIOX, &GPIO_InitStructure);

	// 设置定时器的自动重载值
	TIM_TimeBaseInitStructure.TIM_Period = 1600-1;
	// 设置定时器的预分频系数
	if (pwm->m_TIM_Prescaler!=0&&pwm->m_countFrequency!=0)
	{
		TIM_TimeBaseInitStructure.TIM_Prescaler =pwm->m_TIM_Prescaler;
	}
	else if (((pwm->m_portNum > 1) && (pwm->m_portNum < 8)) || pwm->m_portNum > 11)//APB1
	{
		TIM_TimeBaseInitStructure.TIM_Prescaler = STM32F407_168M_APB1_1MHz_Prescaler;//1us一次计数 1s计数1000000次
		pwm->m_countFrequency=1000000;
	}
	else
	{
		TIM_TimeBaseInitStructure.TIM_Prescaler = STM32F407_168M_APB2_1MHz_Prescaler;//1us一次计数 1s计数1000000次
		pwm->m_countFrequency=1000000;
	}
	// 设置定时器的时钟分频因子为 1
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	// 设置定时器的计数模式为向上计数模式
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	// 根据上述配置初始化 TIM14 的基本参数
	TIM_TimeBaseInit(tim[portIndex].TIMX, &TIM_TimeBaseInitStructure);

	// 设置定时器输出比较模式为 PWM 模式 1
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_Pulse = (TIM_TimeBaseInitStructure.TIM_Period+1)/2;  // 初始脉冲宽度
	// 设置输出极性为低电平有效
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_Low;
	// 使能定时器的输出状态
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;

	uint16_t ccrValue=0;
    // 计算比较寄存器的值
    ccrValue = (uint16_t)(50 / 100.0 * (TIM_TimeBaseInitStructure.TIM_Period + 1));

	// 根据上述配置初始化 TIM1 的输出比较通道 
	if(pwm->m_oc==1)
	{
		TIM_OC1Init(tim[portIndex].TIMX, &TIM_OCInitStructure);
		TIM_OC1PreloadConfig(tim[portIndex].TIMX, TIM_OCPreload_Enable);
		TIM_SetCompare1(tim[portIndex].TIMX, ccrValue);
	}
	else if(pwm->m_oc==2)
	{
		TIM_OC2Init(tim[portIndex].TIMX, &TIM_OCInitStructure);
		TIM_OC2PreloadConfig(tim[portIndex].TIMX, TIM_OCPreload_Enable);
		TIM_SetCompare2(tim[portIndex].TIMX, ccrValue);
	}
	else if(pwm->m_oc==3)
	{
		TIM_OC3Init(tim[portIndex].TIMX, &TIM_OCInitStructure);
		TIM_OC3PreloadConfig(tim[portIndex].TIMX, TIM_OCPreload_Enable);
		TIM_SetCompare3(tim[portIndex].TIMX, ccrValue);
	}
	else if(pwm->m_oc==4)
	{
		TIM_OC4Init(tim[portIndex].TIMX, &TIM_OCInitStructure);
		TIM_OC4PreloadConfig(tim[portIndex].TIMX, TIM_OCPreload_Enable);
		TIM_SetCompare4(tim[portIndex].TIMX, ccrValue);
	}
	// 使能 TIM1 的自动重载寄存器的预装载功能
	TIM_ARRPreloadConfig(tim[portIndex].TIMX, ENABLE);

	 // 高级定时器需要额外使能主输出
	if (pwm->m_portNum == 1 || pwm->m_portNum == 8)
	{
		TIM_CtrlPWMOutputs(tim[portIndex].TIMX, ENABLE);
	}
	// 使能 TIM1 定时器
	TIM_Cmd(tim[portIndex].TIMX, DISABLE);

	
	pwm->m_TIMX=tim[portIndex].TIMX;
	openTIM[portIndex]=pwm;
	((XIODeviceBase*)pwm)->m_mode=mode;
	return true;
}

 void XPWMDeviceSTM32_TIM_ITConfig(XPWMDeviceSTM32 *pwm, bool open)
 {
	if(pwm==NULL)
		return;
	if(open&&XIODeviceBase_isOpen(pwm))
	{
		// 使能定时器  更新中断
    	TIM_ITConfig(pwm->m_TIMX, TIM_IT_Update, ENABLE);
		NVIC_InitTypeDef NVIC_InitStructure;
    	// NVIC 配置
		switch ( pwm->m_portNum)
		{
			case 1:NVIC_InitStructure.NVIC_IRQChannel = TIM1_UP_TIM10_IRQn;XInterrupt_addTIMxCallback(10,TIMCallback,pwm); break;
			case 2:NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;XInterrupt_addTIMxCallback(2,TIMCallback,pwm);break;
			case 3:NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;XInterrupt_addTIMxCallback(3,TIMCallback,pwm);break;
			case 4:NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;XInterrupt_addTIMxCallback(4,TIMCallback,pwm);break;
			case 5:NVIC_InitStructure.NVIC_IRQChannel = TIM5_IRQn;XInterrupt_addTIMxCallback(5,TIMCallback,pwm);break;
			case 8:NVIC_InitStructure.NVIC_IRQChannel = TIM8_UP_TIM13_IRQn;XInterrupt_addTIMxCallback(13,TIMCallback,pwm);break;
			case 9:NVIC_InitStructure.NVIC_IRQChannel = TIM1_BRK_TIM9_IRQn;XInterrupt_addTIMxCallback(9,TIMCallback,pwm);break;
			case 10:NVIC_InitStructure.NVIC_IRQChannel = TIM1_UP_TIM10_IRQn;XInterrupt_addTIMxCallback(10,TIMCallback,pwm);break;
			case 11:NVIC_InitStructure.NVIC_IRQChannel = TIM1_TRG_COM_TIM11_IRQn;XInterrupt_addTIMxCallback(11,TIMCallback,pwm);break;
			case 12:NVIC_InitStructure.NVIC_IRQChannel = TIM8_BRK_TIM12_IRQn;XInterrupt_addTIMxCallback(12,TIMCallback,pwm);break;
			case 13:NVIC_InitStructure.NVIC_IRQChannel = TIM8_UP_TIM13_IRQn;XInterrupt_addTIMxCallback(13,TIMCallback,pwm);break;
			case 14:NVIC_InitStructure.NVIC_IRQChannel = TIM8_TRG_COM_TIM14_IRQn;XInterrupt_addTIMxCallback(14,TIMCallback,pwm);break;
		default:
			break;
		}
    	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 15;
    	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
   	 	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    	NVIC_Init(&NVIC_InitStructure);
		
	}
	else
	{
		// 使能定时器  更新中断
    	TIM_ITConfig(pwm->m_TIMX, TIM_IT_Update, DISABLE);
	}
 }
 void XPWMDeviceSTM32_setTIM_ITCallback(XPWMDeviceSTM32 *pwm, void (*callback)(void *), void *userData)
 {
	if(pwm==NULL)
		return ;
	pwm->m_updateCallback=callback;
	pwm->m_userData=userData;
 }
 size_t VXIODevice_write(XPWMDeviceSTM32 *pwm, const char *data, size_t maxSize)
 {
     if (pwm->m_parent.m_parent.m_mode & XIODeviceBase_WriteOnly) {
         GPIO_WriteBit(pwm->m_gpio.GPIOX, pwm->m_gpio.GPIO_Pin_X, *((bool *)data));
         return 1;
     }
     return 0;
 }

size_t VXIODevice_read(XPWMDeviceSTM32 *pwm, char *data, size_t maxSize)
{
    if(pwm->m_parent.m_parent.m_mode&XIODeviceBase_ReadOnly)
	{
		*((bool*)data)=GPIO_ReadInputDataBit(pwm->m_gpio.GPIOX,pwm->m_gpio.GPIO_Pin_X);
		return 1;
	}
	else  if(pwm->m_parent.m_parent.m_mode&XIODeviceBase_WriteOnly)
	{
		*((bool*)data)=GPIO_ReadOutputDataBit(pwm->m_gpio.GPIOX,pwm->m_gpio.GPIO_Pin_X);
		return 1;
	}
    return 0;
}

void VXIODevice_close(XPWMDeviceSTM32 *pwm)
{
	if (!XPWMDeviceBase_isOpen(pwm))
    	return true;//已经关闭了
 	XIODeviceBase_writeFull_base(pwm);
 	TIM_ITConfig(pwm->m_TIMX, TIM_IT_Update, DISABLE);//关闭接收相关中断
 	TIM_Cmd(pwm->m_TIMX, DISABLE); 
 	pwm->m_TIMX = NULL;
 	openTIM[pwm->m_portNum - 1] = NULL;
	//删除中断回调函数
	if(pwm->m_updateCallback!=NULL)
	{
			switch ( pwm->m_portNum)
		{
			case 1:XInterrupt_removeTIMxCallback(10,TIMCallback,pwm); break;
			case 2:XInterrupt_removeTIMxCallback(2,TIMCallback,pwm);break;
			case 3:XInterrupt_removeTIMxCallback(3,TIMCallback,pwm);break;
			case 4:XInterrupt_removeTIMxCallback(4,TIMCallback,pwm);break;
			case 5:XInterrupt_removeTIMxCallback(5,TIMCallback,pwm);break;
			case 8:XInterrupt_removeTIMxCallback(13,TIMCallback,pwm);break;
			case 9:XInterrupt_removeTIMxCallback(9,TIMCallback,pwm);break;
			case 10:XInterrupt_removeTIMxCallback(10,TIMCallback,pwm);break;
			case 11:XInterrupt_removeTIMxCallback(11,TIMCallback,pwm);break;
			case 12:XInterrupt_removeTIMxCallback(12,TIMCallback,pwm);break;
			case 13:XInterrupt_removeTIMxCallback(13,TIMCallback,pwm);break;
			case 14:XInterrupt_removeTIMxCallback(14,TIMCallback,pwm);break;
		default:
			break;
		}
	}
 	((XIODeviceBase*)pwm)->m_mode = XIODeviceBase_NotOpen;
}
// #define TIMX_Handler(port)\
//   if (TIM_GetITStatus(TIM##port, TIM_IT_Update) != RESET) \
//   {\
// 	  TIM_ClearITPendingBit(TIM##port, TIM_IT_Update);\
// 	  openTIM[port-1]->m_updateCallback( openTIM[port-1]->m_userData);\
//   }
// #define TIMX_IRQHandler(port)\
// void TIM##port##_IRQHandler(void)\
// {\
// 	TIMX_Handler(port)\
// }
// #define TIMX_UP_TIMX_IRQHandler(port)\
// void TIM##port##_IRQHandler(void)\
// {\
// 	TIMX_Handler(port)\
// }

//TIMX_IRQHandler(5)

// // TIM1更新中断处理函数
// void TIM1_UP_TIM10_IRQHandler(void) {
//     if (TIM_GetITStatus(TIM1, TIM_IT_Update) != RESET) {
//         TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
//         //printf("定时器中断\n");
//         XStepMotor_timerCallback( AutoCuttingMachine_global()->m_cuttingMotor);
//     }
// }


#endif
#endif