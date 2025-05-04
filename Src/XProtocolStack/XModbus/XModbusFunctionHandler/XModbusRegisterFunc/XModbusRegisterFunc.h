#ifndef XMODBUSREGISTERFUNC_H
#define XMODBUSREGISTERFUNC_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusFuncObject.h"
#include<stdint.h>
#include<stdbool.h>
//功能描述: 处理0x03（读）、0x06（写单个）、0x10（写多个）功能码，操作保持寄存器
//寄存器功能类
typedef struct XModbusRegisterFunc
{
	XModbusFuncObject parent;

}XModbusRegisterFunc;
//创建保持寄存器功能类 要创建几个保持寄存器
XModbusRegisterFunc* XModbusRegisterFunc_new(uint16_t regCount);
//写入寄存器
bool XModbusRegisterFunc_write_uint16_t(XModbusRegisterFunc* regFunc, uint16_t regAddress,uint16_t value);
//写入寄存器 数组的方式
bool XModbusRegisterFunc_write(XModbusRegisterFunc* regFunc, uint16_t regAddress,const char* writeArray,uint16_t arraySize);
// 读取寄存器
bool XModbusRegisterFunc_read(XModbusRegisterFunc* regFunc, uint16_t regAddress, uint16_t regCount,char* readArray, uint16_t readArraySize);
//获取指定地址寄存器的缓冲区地址
uint16_t* XModbusRegisterFunc_at(XModbusRegisterFunc* regFunc, uint16_t regAddress);

//以下都是功能码回调函数

//0x03读保持寄存器接收回调函数 主站是响应报文
XModbusException XModbusRegisterFunc_0x03_RTU_masterRecvHandCallFunc(XModbus* modbus, XModbusFrameData* frameData, XModbusFunctionHandler* FunctionHandler);
//0x03读保持寄存器接收回调函数 从站是请求报文
XModbusException XModbusRegisterFunc_0x03_RTU_slaveRecvHandCallFunc(XModbus* modbus, XModbusFrameData* frameData, XModbusFunctionHandler* FunctionHandler);

//0x06写保持寄存器接收回调函数 从站是请求报文
XModbusException XModbusRegisterFunc_0x06_RTU_slaveRecvHandCallFunc(XModbus* modbus, XModbusFrameData* frameData, XModbusFunctionHandler* FunctionHandler);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFuncCode_H
