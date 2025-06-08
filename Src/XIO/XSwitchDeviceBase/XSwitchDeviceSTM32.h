#ifdef USE_STDPERIPH_DRIVER
#ifndef XSWITCHDEVICESTM32_H
#define XSWITCHDEVICESTM32_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XSwitchDeviceBase.h"
#define XSWITCHDEVICESTM32_VTABLE_SIZE (XSWITCHDEVICEBASE_VTABLE_SIZE)       //XSwitchDeviceSTM32容器虚函数表大小
typedef struct XSwitchDeviceSTM32 XSwitchDeviceSTM32;
#ifdef USE_STDPERIPH_DRIVER
//标准库配置信息
typedef struct XSwitchGPIO
{
    uint16_t GPIO_Pin_X;
    uint32_t GPIOX;
	int GPIO_PuPd;//上下拉
    uint32_t GPIO_Clock;//时钟
}XSwitchGPIO;
#endif
//开关设备
typedef struct XSwitchDeviceSTM32
{
	XSwitchDeviceBase m_parent;//父对象
	XSwitchGPIO m_gpio;
}XSwitchDeviceSTM32;
//初始化类
XVtable* XSwitchDeviceSTM32_class_init();
//开关设备
#ifdef USE_STDPERIPH_DRIVER
XSwitchDeviceSTM32* XSwitchDeviceSTM32_create(XSwitchGPIO* gpio);
#endif
//初始化
void XSwitchDeviceSTM32_init(XSwitchDeviceSTM32* sw);
#define XSwitchDeviceSTM32_setStateChangeCallback 		XSwitchDeviceBase_setStateChangeCallback
#define XSwitchDeviceSTM32_setState_base				XSwitchDeviceBase_setState_base
#define XSwitchDeviceSTM32_getState_base				XSwitchDeviceBase_getState_base
#define XSwitchDeviceSTM32_isOpen		 				XSwitchDeviceBase_isOpen
#define XSwitchDeviceSTM32_open_base		 			XSwitchDeviceBase_open_base
#define XSwitchDeviceSTM32_close_base		    		XSwitchDeviceBase_close_base
#define XSwitchDeviceSTM32_setDevice_base 				XSwitchDeviceBase_setDevice_base
#define XSwitchDeviceSTM32_delete_base					XSwitchDeviceBase_delete_base
#define XSwitchDeviceSTM32_poll_base		 			XSwitchDeviceBase_poll_base
#ifdef __cplusplus
}
#endif
#endif

#endif