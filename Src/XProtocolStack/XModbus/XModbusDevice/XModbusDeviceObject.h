#ifndef XMODBUSDEVICEOBJECT_H
#define XMODBUSDEVICEOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusProto.h"
#include"XModbusEnum.h"
typedef struct XModbus XModbus;
typedef struct XModbusFrame XModbusFrame;
//功能码处理基类结构体
typedef struct XModbusDeviceObject
{
	void* data;//数据
	void* userData;//用户数据
	void (*cb)(void* userData);//回调函数
}XModbusDeviceObject;
void XModbusDeviceObject_init(XModbusDeviceObject* de);
void XModbusDeviceObject_setCallback(XModbusDeviceObject* de, void (*cb)(void* userData));
void XModbusDeviceObject_setUserData(XModbusDeviceObject* de, void* userData);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFuncCode_H
