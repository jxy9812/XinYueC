#ifndef XPWMDEVICE_H
#define XPWMDEVICE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XIODevice.h"
//XPWMDevice虚函数表
extern XVtable* XPWMDeviceVtable;
#define XPWMDEVICE_VTABLE_SIZE (XIODEVICEBASE_VTABLE_SIZE+7)       //XPWMDevice容器虚函数表大小
//XPWMDevice虚函数表枚举
enum XPWMDeviceVtableEnum
{
	EXPWMDeviceBase_SetFrequency = XIODEVICEBASE_VTABLE_SIZE,
	EXPWMDeviceBase_SetDutyCycle,
	EXPWMDeviceBase_Start,
	EXPWMDeviceBase_Stop,
	EXPWMDeviceBase_IsRunning,
	EXPWMDeviceBase_GetFrequency,
	EXPWMDeviceBase_GetDutyCycle,
};
typedef struct XPWMDeviceBase XPWMDeviceBase;

//pwm设备
typedef struct XPWMDeviceBase
{
	XIODeviceBase m_parent;//父对象
	bool m_isRun;//是否运行
	uint8_t m_dutyCycle;//占空比
	size_t m_frequency;//频率
	void* m_userData;//用户数据
	void (*m_runChangeCallback)(XPWMDeviceBase* pwm);//运行状态改变回调函数
}XPWMDeviceBase;
//初始化类
void XPWMDeviceBase_class_init();
//pwm设备
XPWMDeviceBase* XPWMDeviceBase_new(XVtable* vtable);
void XPWMDeviceBase_init(XPWMDeviceBase* pwm, XVtable* vtable);
//设置运行状态改变回调函数
void XPWMDeviceBase_setRunChangeCallback(XPWMDeviceBase* sw, void (*callback)(XPWMDeviceBase* pwm));
//设置频率  T(s)=1/F(HZ) 周期  一个周期
void XPWMDeviceBase_setFrequency_base(XPWMDeviceBase* pwm,size_t f);
//设置占空比 T(s)=1/F(HZ)*D(0`100)/100   电平翻转周期
void XPWMDeviceBase_setDutyCycle_base(XPWMDeviceBase* pwm, uint8_t d);
//启动函数 需要重载实现
void XPWMDeviceBase_start_base(XPWMDeviceBase* pwm);
//停止函数 需要重载实现
void XPWMDeviceBase_stop_base(XPWMDeviceBase* pwm);
bool XPWMDeviceBase_isRunning_base(XPWMDeviceBase* pwm);
//频率  T(s)=1/F(HZ) 周期  一个周期
size_t XPWMDeviceBase_getFrequency_base(XPWMDeviceBase* pwm);
//占空比 T(s)=1/F(HZ)*D(0`100)/100   电平翻转周期
uint8_t XPWMDeviceBase_getDutyCycle_base(XPWMDeviceBase* pwm);
#define XPWMDeviceBase_isOpen_base XIODevice_isOpen_base
#define XPWMDeviceBase_open_base XIODevice_open_base
#define XPWMDeviceBase_close_base XIODevice_close_base
#define XPWMDeviceBase_setDevice_base XIODevice_setDevice_base
#define XPWMDeviceBase_free_base XIODevice_free_base
#define XPWMDeviceBase_poll_base XIODevice_poll_base
#ifdef __cplusplus
}
#endif
#endif