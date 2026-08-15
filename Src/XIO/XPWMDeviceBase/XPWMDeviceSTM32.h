#ifdef USE_STDPERIPH_DRIVER
#ifndef XPWMDEVICESTM32_H
#define XPWMDEVICESTM32_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XPWMDeviceBase.h"

#define XPWMDEVICESTM32_VTABLE_SIZE (XPWMDEVICE_VTABLE_SIZE)       //XPWMDeviceSTM32虚函数表大小
typedef struct XPWMDeviceSTM32 XPWMDeviceSTM32;

#ifdef USE_STDPERIPH_DRIVER
//标准库配置信息
typedef struct XPWMGPIO
{
	uint8_t  GPIO_PinSourceX;
    uint16_t GPIO_Pin_X;
    uint32_t GPIOX;
    uint32_t GPIO_Clock;//时钟
}XPWMGPIO;
#endif
//pwm设备
typedef struct XPWMDeviceSTM32
{
	XPWMDeviceBase m_class;//父对象
	XPWMGPIO m_gpio;//gpio
	uint32_t m_TIMX;//定时器
	uint8_t m_oc;//通道数
	uint8_t m_portNum;//端口号
	uint16_t m_TIM_Prescaler;//预分频系数
	uint32_t m_countFrequency;//当前预分频系数下的计数频率
	void* m_userData;
	void(*m_updateCallback)(void*);//定时器更新回调
}XPWMDeviceSTM32;
//初始化类
XVtable* XPWMDeviceSTM32_class_init();
#ifdef USE_STDPERIPH_DRIVER
//pwm设备
XPWMDeviceSTM32* XPWMDeviceSTM32_create_ex(XMemoryType memory,  XPWMGPIO* gpio);
bool XPWMDeviceSTM32_open_base(XPWMDeviceSTM32* pwm, XIODeviceBaseMode mode, uint8_t portNum, uint8_t oc, void(*updateCallback)(void*));
void XPWMDeviceSTM32_TIM_ITConfig(XPWMDeviceSTM32* pwm,bool open);
void XPWMDeviceSTM32_setTIM_ITCallback(XPWMDeviceSTM32* pwm,void(*callback)(void*),void*userData);
#endif
void XPWMDeviceSTM32_init(XPWMDeviceSTM32* pwm);
//设置运行状态改变回调函数
#define XPWMDeviceSTM32_setRunChangeCallback		XPWMDeviceBase_setRunChangeCallback
//设置频率  T(s)=1/F(HZ) 周期  一个周期
#define XPWMDeviceSTM32_setFrequency_base			XPWMDeviceBase_setFrequency_base
//设置占空比 T(s)=1/F(HZ)*D(0`100)/100   电平翻转周期
#define XPWMDeviceSTM32_setDutyCycle_base			XPWMDeviceBase_setDutyCycle_base
//启动函数 需要重载实现
#define XPWMDeviceSTM32_start_base					XPWMDeviceBase_start_base
//停止函数 需要重载实现
#define XPWMDeviceSTM32_stop_base					XPWMDeviceBase_stop_base
#define XPWMDeviceSTM32_isRunning_base				XPWMDeviceBase_isRunning_base
//频率  T(s)=1/F(HZ) 周期  一个周期
#define XPWMDeviceSTM32_getFrequency_base			XPWMDeviceBase_getFrequency_base
//占空比 T(s)=1/F(HZ)*D(0`100)/100   电平翻转周期
#define XPWMDeviceSTM32_getDutyCycle_base			XPWMDeviceBase_getDutyCycle_base
#define XPWMDeviceSTM32_isOpen						XPWMDeviceBase_isOpen
//#define XPWMDeviceSTM32_open_base					XPWMDeviceBase_open_base
#define XPWMDeviceSTM32_close_base					XPWMDeviceBase_close_base
#define XPWMDeviceSTM32_setDevice_base   			XPWMDeviceBase_setDevice_base
#define XPWMDeviceSTM32_delete_base					XPWMDeviceBase_delete_base
#define XPWMDeviceSTM32_poll_base					XPWMDeviceBase_poll_base
#ifdef __cplusplus
}
#endif
#endif


/* XClass create API default-memory wrappers. */
#undef XPWMDeviceSTM32_create
#define XPWMDeviceSTM32_create(...) XPWMDeviceSTM32_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif