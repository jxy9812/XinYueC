#ifndef XSWITCHDEVICE_H
#define XSWITCHDEVICE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XIODevice.h"
//XSwitchDevice虚函数表
extern XVtable* XSwitchDeviceVtable;
#define XSWITCHDEVICE_VTABLE_SIZE (XIODEVICE_VTABLE_SIZE+2)       //XSwitchDevice容器虚函数表大小
//XSwitchDevice虚函数表枚举
enum XSwitchDeviceVtableEnum
{
	EXSwitchDevice_SetState = XIODEVICE_VTABLE_SIZE,
	EXSwitchDevice_GetState,
};
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
	bool buffer;//存储状态
	bool state;//状态   开或关
	XSwitchDevice_PortFunc m_port;//开关设备接口
}XSwitchDevice;
//初始化类
void XSwitchDevice_class_init();
//开关设备
XSwitchDevice* XSwitchDevice_new(XSwitchDevice_PortFuncInit* port);
//初始化
void XSwitchDevice_init(XSwitchDevice* sw,XSwitchDevice_PortFuncInit* port);
//默认轮询方法
void XSwitchDevice_pollDefaultMethod(XSwitchDevice* sw);
//设置开关设备状态
void XSwitchDevice_setState_base(XSwitchDevice* sw,bool state);
//获取状态
bool XSwitchDevice_getState_base(XSwitchDevice* sw);
#define XSwitchDevice_isOpen_base XIODevice_isOpen_base
#define XSwitchDevice_open_base XIODevice_open_base
#define XSwitchDevice_close_base XIODevice_close_base
#define XSwitchDevice_setDevice_base XIODevice_setDevice_base
#define XSwitchDevice_free_base XIODevice_free_base
#define XSwitchDevice_poll_base XIODevice_poll_base
#ifdef __cplusplus
}
#endif
#endif