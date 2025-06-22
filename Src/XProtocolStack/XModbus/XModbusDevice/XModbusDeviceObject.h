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

}XModbusDeviceObject;

#ifdef __cplusplus
}
#endif
#endif // !XModbusFuncCode_H
