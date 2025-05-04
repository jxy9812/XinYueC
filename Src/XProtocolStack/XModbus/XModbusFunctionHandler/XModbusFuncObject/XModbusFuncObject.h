#ifndef XMODBUSFUNCOBJECT_H
#define XMODBUSFUNCOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusProto.h"
typedef struct XModbus XModbus;
typedef struct XModbusFrameData XModbusFrameData;
typedef struct XModbusFunctionHandler XModbusFunctionHandler;
/*! \brief 功能码处理函数指针类型 */
typedef XModbusException(*pXModbusFunctionHandler) (XModbus* modbus,XModbusFrameData* frameData, XModbusFunctionHandler* FunctionHandler);
//功能码处理基类结构体
typedef struct XModbusFuncObject
{
	void* data;//数据

}XModbusFuncObject;

#ifdef __cplusplus
}
#endif
#endif // !XModbusFuncCode_H
