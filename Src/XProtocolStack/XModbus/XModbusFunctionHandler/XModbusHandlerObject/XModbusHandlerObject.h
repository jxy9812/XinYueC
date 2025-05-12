#ifndef XMODBUSFUNCOBJECT_H
#define XMODBUSFUNCOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusProto.h"
#include"XModbusEnum.h"
typedef struct XModbus XModbus;
typedef struct XModbusFrame XModbusFrame;
typedef struct XModbusFunctionHandler XModbusFunctionHandler;
/*! \brief 功能码处理函数指针类型 */
typedef XModbusException(*pXModbusFunctionHandler) (XModbus* modbus,XModbusFrame* frameData, XModbusFunctionHandler* FunctionHandler);
//功能码处理基类结构体
typedef struct XModbusHandlerObject
{
	void* data;//数据

}XModbusHandlerObject;

#ifdef __cplusplus
}
#endif
#endif // !XModbusFuncCode_H
