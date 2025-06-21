#ifndef XMODBUSFUNCOBJECT_H
#define XMODBUSFUNCOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusProto.h"
#include"XModbusEnum.h"
typedef struct XModbus XModbus;
typedef struct XModbusFrame XModbusFrame;
//功能码处理基类结构体
typedef struct XModbusHandlerObject
{
	void* data;//数据

}XModbusHandlerObject;

#ifdef __cplusplus
}
#endif
#endif // !XModbusFuncCode_H
