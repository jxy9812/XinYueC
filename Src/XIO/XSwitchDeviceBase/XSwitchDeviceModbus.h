#ifndef XSWITCHDEVICEMODBUS_H
#define XSWITCHDEVICEMODBUS_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XSwitchDeviceBase.h"
#define XSWITCHDEVICEMODBUS_VTABLE_SIZE (XSWITCHDEVICEBASE_VTABLE_SIZE)       //XSwitchDeviceModbus容器虚函数表大小
//开关设备
typedef struct XSwitchDeviceModbus
{
	XSwitchDeviceBase m_class;//父对象
	XModbusDigitalSwitch* m_ds;
	uint16_t m_portNum;//端口号
}XSwitchDeviceModbus;
//初始化类
XVtable* XSwitchDeviceModbus_class_init();
//开关设备
XSwitchDeviceModbus* XSwitchDeviceModbus_create(XModbusDigitalSwitch* ds, uint16_t portNum);
//初始化
void XSwitchDeviceModbus_init(XSwitchDeviceModbus* sw);
#define XSwitchDeviceModbus_setStateChangeCallback 		XSwitchDeviceBase_setStateChangeCallback
#define XSwitchDeviceModbus_setState_base				XSwitchDeviceBase_setState_base
#define XSwitchDeviceModbus_getState_base				XSwitchDeviceBase_getState_base
#define XSwitchDeviceModbus_isOpen		 				XSwitchDeviceBase_isOpen
#define XSwitchDeviceModbus_open_base		 			XSwitchDeviceBase_open_base
#define XSwitchDeviceModbus_close_base		    		XSwitchDeviceBase_close_base
#define XSwitchDeviceModbus_setDevice_base 				XSwitchDeviceBase_setDevice_base
#define XSwitchDeviceModbus_delete_base					XSwitchDeviceBase_delete_base
#define XSwitchDeviceModbus_poll_base		 			XSwitchDeviceBase_poll_base
#ifdef __cplusplus
}
#endif
#endif