#ifndef XSWITCHDEVICEBASE_H
#define XSWITCHDEVICEBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XIODevice.h"
#define XSWITCHDEVICEBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XSwitchDeviceBase))       //XSwitchDeviceBase容器虚函数表大小
//XSwitchDevice虚函数表枚举
XCLASS_DEFINE_BEGING(XSwitchDeviceBase)
XCLASS_DEFINE_ENUM(XSwitchDeviceBase,SetState) = XCLASS_VTABLE_GET_SIZE(XIODevice),
XCLASS_DEFINE_ENUM(XSwitchDeviceBase,GetState),
XCLASS_DEFINE_END(XSwitchDeviceBase)
typedef enum//触发方式
{
	XSwitchDeviceBase_Trigger_High,//高电平触发 
	XSwitchDeviceBase_Trigger_Low,//低电平触发
	XSwitchDeviceBase_Trigger_None,//无触发
}XSwitchDeviceBaseTriggerMode;
//开关设备
typedef struct XSwitchDeviceBase
{
	XIODevice m_class;//父对象
	bool m_buffer;//存储状态
	bool m_state;//状态   开或关
	XSwitchDeviceBaseTriggerMode m_triggerMode;//触发方式
	void (*m_stateChangeCallback)(XSwitchDeviceBase* io);//状态改变回调函数
}XSwitchDeviceBase;
//初始化类
XVtable* XSwitchDeviceBase_class_init();
//开关设备
XSwitchDeviceBase* XSwitchDeviceBase_create();
//初始化
void XSwitchDeviceBase_init(XSwitchDeviceBase* sw);
//设置状态改变回调函数
void XSwitchDeviceBase_setStateChangeCallback(XSwitchDeviceBase* sw, void (*callback)(XSwitchDeviceBase* io));
//设置触发方式 设备开的状态时候的电平
void XSwitchDeviceBase_setTriggerMode(XSwitchDeviceBase* sw, XSwitchDeviceBaseTriggerMode mode);
XSwitchDeviceBaseTriggerMode XSwitchDeviceBase_getTriggerMode(XSwitchDeviceBase* sw);
//设置开关设备状态
void XSwitchDeviceBase_setState_base(XSwitchDeviceBase* sw,bool state);
//获取状态
bool XSwitchDeviceBase_getState_base(XSwitchDeviceBase* sw);
#define XSwitchDeviceBase_isOpen			 XIODevice_isOpen
#define XSwitchDeviceBase_open_base			 XIODevice_open_base
#define XSwitchDeviceBase_close_base		 XIODevice_close_base
#define XSwitchDeviceBase_setDevice_base	 XIODevice_setDevice_base
#define XSwitchDeviceBase_delete_base		 XIODevice_delete_base
#define XSwitchDeviceBase_poll_base			 XIODevice_poll_base

#if defined(USE_STDPERIPH_DRIVER) 
#include"XSwitchDeviceSTM32.h"
#endif
#ifdef __cplusplus
}
#endif
#endif