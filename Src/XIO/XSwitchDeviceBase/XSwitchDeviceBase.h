#ifndef XSWITCHDEVICE_H
#define XSWITCHDEVICE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XIODeviceBase.h"
#define XSWITCHDEVICEBASE_VTABLE_SIZE (XIODEVICEBASE_VTABLE_SIZE+2)       //XSwitchDeviceBase容器虚函数表大小
//XSwitchDevice虚函数表枚举
enum XSwitchDeviceBaseVtableEnum
{
	EXSwitchDeviceBase_SetState = XIODEVICEBASE_VTABLE_SIZE,
	EXSwitchDeviceBase_GetState,
};
typedef struct XSwitchDeviceBase XSwitchDeviceBase;
//开关设备
typedef struct XSwitchDeviceBase
{
	XIODeviceBase m_parent;//父对象
	bool m_buffer;//存储状态
	bool m_state;//状态   开或关
	void (*m_stateChangeCallback)(XSwitchDeviceBase* io);//状态改变回调函数
}XSwitchDeviceBase;
//初始化类
XVtable* XSwitchDeviceBase_class_init();
//开关设备
XSwitchDeviceBase* XSwitchDeviceBase_create(XVtable* vtable);
//初始化
void XSwitchDeviceBase_init(XSwitchDeviceBase* sw, XVtable* vtable);
//设置状态改变回调函数
void XSwitchDeviceBase_setStateChangeCallback(XSwitchDeviceBase* sw, void (*callback)(XSwitchDeviceBase* io));
//设置开关设备状态
void XSwitchDeviceBase_setState_base(XSwitchDeviceBase* sw,bool state);
//获取状态
bool XSwitchDeviceBase_getState_base(XSwitchDeviceBase* sw);
#define XSwitchDeviceBase_isOpen		 XIODeviceBase_isOpen
#define XSwitchDeviceBase_open_base		 XIODeviceBase_open_base
#define XSwitchDeviceBase_close_base	 XIODeviceBase_close_base
#define XSwitchDeviceBase_setDevice_base XIODeviceBase_setDevice_base
#define XSwitchDeviceBase_free_base		 XIODeviceBase_free_base
#define XSwitchDeviceBase_poll_base		 XIODeviceBase_poll_base
#ifdef __cplusplus
}
#endif
#endif