#ifndef XSWITCHDEVICE_H
#define XSWITCHDEVICE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XIODevice.h"
typedef struct XSwitchDevice XSwitchDevice;
//开关设备接口
typedef struct XSwitchDevice_PortFunc
{
	void (*stateChangeCallback)(XSwitchDevice* io);//状态改变回调函数
}XSwitchDevice_PortFunc;
//开关设备初始化接口
typedef struct XSwitchDevice_PortFuncInit
{
	XIODevice_PortFuncInit parentPort;//父对象接口
	XSwitchDevice_PortFunc SwitchPort;//子类开关接口
}XSwitchDevice_PortFuncInit;
//开关设备
typedef struct XSwitchDevice
{
	XIODevice m_parent;//父对象
	bool state;//状态   开或关
	XSwitchDevice_PortFunc m_port;//开关设备接口
}XSwitchDevice;
//开关设备
XSwitchDevice* XSwitchDevice_new(XSwitchDevice_PortFuncInit* port);
//初始化
void XSwitchDevice_init(XSwitchDevice* sw,XSwitchDevice_PortFuncInit* port);
//默认轮询方法
void XSwitchDevice_pollDefaultMethod(XSwitchDevice* sw);
//设置开关设备状态
void XSwitchDevice_setState(XSwitchDevice* sw,bool state);
//获取状态
bool XSwitchDevice_getState(XSwitchDevice* sw);
#define XSwitchDevice_poll(sw) XIODevice_poll(sw);
#ifdef __cplusplus
}
#endif
#endif