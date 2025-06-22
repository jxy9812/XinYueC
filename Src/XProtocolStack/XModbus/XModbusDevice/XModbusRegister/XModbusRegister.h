#ifndef XMODBUSREGISTER_H
#define XMODBUSREGISTER_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct XModbus XModbus;
#include"XModbusDeviceObject.h"
#include<stdint.h>
#include<stdbool.h>
//功能描述: 处理0x03（读）0x04(只读)、0x06（写单个）、0x10（写多个）功能码，操作保持寄存器
//寄存器功能类
typedef struct XModbusRegister
{
	XModbusDeviceObject parent;

}XModbusRegister;
//创建保持寄存器功能类 要创建几个保持寄存器
XModbusRegister* XModbusRegister_create(uint16_t regCount);
void XModbusRegister_delete(XModbusRegister* pRegFunc);
//写入寄存器
bool XModbusRegister_write_uint16_t(XModbusRegister* regFunc, uint16_t regAddress,uint16_t value);
//写入寄存器 数组的方式
bool XModbusRegister_write(XModbusRegister* regFunc, uint16_t regAddress, uint16_t regCount,const uint8_t* writeArray);
// 读取寄存器
bool XModbusRegister_read(XModbusRegister* regFunc, uint16_t regAddress, uint16_t regCount, uint8_t* readArray, uint16_t readArraySize);
//获取指定地址寄存器的缓冲区地址
uint16_t* XModbusRegister_at(XModbusRegister* regFunc, uint16_t regAddress);

//以下都是功能码回调函数

//0x03读保持寄存器接收回调函数 主站是响应报文
void XModbusRegister_0x03_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand);
//0x04读输入寄存器接收回调函数 主站是响应报文
void XModbusRegister_0x04_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand);
//0x06写保持寄存器接收回调函数 主站是响应报文
void XModbusRegister_0x06_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand);


//0x03读保持寄存器接收回调函数 从站接收是请求报文
void XModbusRegister_0x03_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand);
//0x04读输入寄存器接收回调函数 从站接收是请求报文
void XModbusRegister_0x04_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand);
//0x06写保持寄存器接收回调函数 从站接收是请求报文
void XModbusRegister_0x06_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand);
//0x10写多个保持寄存器接收回调函数 从站接收是请求报文
void XModbusRegister_0x10_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFuncCode_H
