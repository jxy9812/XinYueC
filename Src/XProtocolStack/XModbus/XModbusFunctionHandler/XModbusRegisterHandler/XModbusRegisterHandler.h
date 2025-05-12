#ifndef XMODBUSREGISTERHANDLER_H
#define XMODBUSREGISTERHANDLER_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusHandlerObject.h"
#include<stdint.h>
#include<stdbool.h>
//功能描述: 处理0x03（读）0x04(只读)、0x06（写单个）、0x10（写多个）功能码，操作保持寄存器
//寄存器功能类
typedef struct XModbusRegisterHandler
{
	XModbusHandlerObject parent;

}XModbusRegisterHandler;
//创建保持寄存器功能类 要创建几个保持寄存器
XModbusRegisterHandler* XModbusRegisterHandler_new(uint16_t regCount);
void XModbusRegisterHandler_free(XModbusRegisterHandler* pRegFunc);
//写入寄存器
bool XModbusRegisterHandler_write_uint16_t(XModbusRegisterHandler* regFunc, uint16_t regAddress,uint16_t value);
//写入寄存器 数组的方式
bool XModbusRegisterHandler_write(XModbusRegisterHandler* regFunc, uint16_t regAddress, uint16_t regCount,const char* writeArray);
// 读取寄存器
bool XModbusRegisterHandler_read(XModbusRegisterHandler* regFunc, uint16_t regAddress, uint16_t regCount,char* readArray, uint16_t readArraySize);
//获取指定地址寄存器的缓冲区地址
uint16_t* XModbusRegisterHandler_at(XModbusRegisterHandler* regFunc, uint16_t regAddress);

//以下都是功能码回调函数

//0x03读保持寄存器接收回调函数 主站是响应报文
XModbusException XModbusRegisterHandler_0x03_RTU_masterRecvHandCallFunc(XModbus* modbus, XModbusFrame* recvFrame, XModbusFunctionHandler* FunctionHandler);

//0x03读保持寄存器接收回调函数 从站接收是请求报文
XModbusException XModbusRegisterHandler_0x03_RTU_slaveRecvHandCallFunc(XModbus* modbus, XModbusFrame* recvFrame, XModbusFunctionHandler* FunctionHandler);
//0x04读输入寄存器接收回调函数 从站接收是请求报文
XModbusException XModbusRegisterHandler_0x04_RTU_slaveRecvHandCallFunc(XModbus* modbus, XModbusFrame* recvFrame, XModbusFunctionHandler* FunctionHandler);
//0x06写保持寄存器接收回调函数 从站接收是请求报文
XModbusException XModbusRegisterHandler_0x06_RTU_slaveRecvHandCallFunc(XModbus* modbus, XModbusFrame* recvFrame, XModbusFunctionHandler* FunctionHandler);
//0x10写多个保持寄存器接收回调函数 从站接收是请求报文
XModbusException XModbusRegisterHandler_0x10_RTU_slaveRecvHandCallFunc(XModbus* modbus, XModbusFrame* recvFrame, XModbusFunctionHandler* FunctionHandler);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFuncCode_H
