#ifndef XMODBUSCOILS_H
#define XMODBUSCOILS_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusHandlerObject.h"
#include<stdint.h>
#include<stdbool.h>
//功能描述: 处理0x01（读线圈）、0x02(读离散)、0x05（写单个线圈）、0x0F（写多个线圈）功能码，操作线圈状态
typedef struct XModbusCoilsDiscHandler
{
	XModbusHandlerObject parent;
	uint16_t  count;//数量
}XModbusCoilsDiscHandler;
//创建线圈或离散功能类 要创建几个离散或线圈
XModbusCoilsDiscHandler* XModbusCoilsDiscHandler_create(uint16_t count);
void XModbusCoilsDiscHandler_delete(XModbusCoilsDiscHandler* pRegHandler);
//写入线圈或离散
bool XModbusCoilsDiscHandler_write(XModbusCoilsDiscHandler* pRegHandler, uint16_t address, uint16_t count, const uint8_t* writeArray);
// 读取线圈或离散
bool XModbusCoilsDiscHandler_read(XModbusCoilsDiscHandler* pRegHandler, uint16_t address, uint16_t count, uint8_t* readArray, uint16_t readArraySize);
//获取指定地址寄存器的线圈或离散状态
bool XModbusCoilsDiscHandler_at(XModbusCoilsDiscHandler* pRegHandler, uint16_t regAddress);
/*    以下都是功能码回调函数*/
//0x01读线圈接收回调函数 主站是响应报文
void XModbusCoilsDiscHandler_0x01_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusHandlerObject* hand);
//0x02读离散接收回调函数 主站是响应报文
void XModbusCoilsDiscHandler_0x02_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusHandlerObject* hand);

//0x01读线圈接收回调函数 从站接收是请求报文
void XModbusCoilsDiscHandler_0x01_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusHandlerObject* hand);
//0x02读离散接收回调函数 从站接收是请求报文
void XModbusCoilsDiscHandler_0x02_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusHandlerObject* hand);
//0x05写单个线圈接收回调函数 从站接收是请求报文
void XModbusCoilsDiscHandler_0x05_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusHandlerObject* hand);
//0x0F写多个线圈接收回调函数 从站接收是请求报文
void XModbusCoilsDiscHandler_0x0F_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusHandlerObject* hand);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFuncCode_H
