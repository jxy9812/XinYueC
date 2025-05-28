#ifndef XPWMDEVICE_H
#define XPWMDEVICE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XIODevice.h"
//XPWMDevice虚函数表
extern XVtable* XPWMDeviceVtable;
#define XPWMDEVICE_VTABLE_SIZE (XIODEVICE_VTABLE_SIZE+7)       //XPWMDevice容器虚函数表大小
//XPWMDevice虚函数表枚举
enum XPWMDeviceVtableEnum
{
	EXPWMDevice_SetFrequency = XIODEVICE_VTABLE_SIZE,
	EXPWMDevice_SetDutyCycle,
	EXPWMDevice_Start,
	EXPWMDevice_Stop,
	EXPWMDevice_IsRunning,
	EXPWMDevice_GetFrequency,
	EXPWMDevice_GetDutyCycle,
};
typedef struct XPWMDevice XPWMDevice;
//pwm设备接口
typedef struct XPWMDevice_PortFunc
{
	void (*start)(XPWMDevice* pwm);
	void (*stop)(XPWMDevice* pwm);
	void (*runChangeCallback)(XPWMDevice* pwm);//运行状态改变回调函数
}XPWMDevice_PortFunc;
//pwm设备初始化接口
typedef struct XPWMDevice_PortFuncInit
{
	XIODevice_PortFuncInit parentPort;//父对象接口
	//XTimer_PortFuncInit timerPort;//定时器接口
	XPWMDevice_PortFunc pwmPort;//子类开关接口
}XPWMDevice_PortFuncInit;
//pwm设备
typedef struct XPWMDevice
{
	XIODevice m_parent;//父对象
	//XTimerBase* m_timer;
	bool m_isRun;//是否运行
	uint8_t m_dutyCycle;//占空比
	size_t m_frequency;//频率
	void* data;//用户数据
	XPWMDevice_PortFunc m_port;//pwm设备接口
}XPWMDevice;
//初始化类
void XPWMDevice_class_init();
//pwm设备
XPWMDevice* XPWMDevice_new(XPWMDevice_PortFuncInit* port);
void XPWMDevice_init(XPWMDevice* pwm,XPWMDevice_PortFuncInit* port);
//设置频率  T(s)=1/F(HZ) 周期  一个周期
void XPWMDevice_setFrequency(XPWMDevice* pwm,size_t f);
//设置占空比 T(s)=1/F(HZ)*D(0`100)/100   电平翻转周期
void XPWMDevice_setDutyCycle(XPWMDevice* pwm, uint8_t d);
void XPWMDevice_start(XPWMDevice* pwm);
void XPWMDevice_stop(XPWMDevice* pwm);
bool XPWMDevice_isRunning(XPWMDevice* pwm);
//频率  T(s)=1/F(HZ) 周期  一个周期
size_t XPWMDevice_getFrequency(XPWMDevice* pwm);
//占空比 T(s)=1/F(HZ)*D(0`100)/100   电平翻转周期
uint8_t XPWMDevice_getDutyCycle(XPWMDevice* pwm);
#define XPWMDevice_isOpen XIODevice_isOpen
#define XPWMDevice_open XIODevice_open
#define XPWMDevice_close XIODevice_close
#define XPWMDevice_setDevice XIODevice_setDevice
#define XPWMDevice_free XIODevice_free
#define XPWMDevice_poll XIODevice_poll
#ifdef __cplusplus
}
#endif
#endif