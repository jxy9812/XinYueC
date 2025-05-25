#ifndef XMODBUSTEST_PORT_H
#define XMODBUSTEST_PORT_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XProtocolStackTest.h"
#include"XModbusRtu.h"
#include"XMemory.h"
#include"XCrc.h"
#include"XModbusFrame.h"

/*Modbus测试接口头文件
不同的操作系统要实现下面几个函数即可启用,Windows接口以实现
移植可以查看Windows实现
*/
void XModbusTest_SerialEnable(XIODevice* io, bool xRxEnable, bool xTxEnable);
// 打开串口
bool XModbusTest_SerialOpen(XIODevice* io, XIODeviceBase mode);
bool XModbusTest_SerialInit(XModbus* modbus, uint8_t port, uint32_t baudRate, XModbusParity parity);
//获取一个字节
bool XModbusTest_GetByte(XModbus* modbus, uint8_t* byte);
bool XModbusTest_readByte(XIODevice* io, char* data, size_t size);
//发送一个字节
bool XModbusTest_PutByte(XModbus* modbus, uint8_t Byte);
bool XModbusTest_writeByte(XIODevice* io, XCircularQueue* queue);
//定时器启动
void XModbusTest_XTimer_Start(XTimer* timer);
//定时器停止
void XModbusTest_XTimer_Stop(XTimer* timer);
//定时器创建
void XModbusTest_XTimerCreat(XTimer* timer);
/*
* XModbusTest_SerialPoll 移植的时候不是必须要实现的 
* 如果使用中断的方式，在合适的时机调用下面两个函数指针即可
* 然后将XModbusTest_SerialPoll的代码删掉
 //接收缓冲区空时调用
 modbus->pxMBFrameCBByteReceived(modbus);
 //写入缓冲区空时调用
 modbus->pxMBFrameCBTransmitterEmpty(modbus);
*/
//控制读写
void XModbusTest_SerialPoll(XModbus* modbus);
//开启接收任务
void XModbusTest_threadReceiveCreate(XModbus* modbus);
#ifdef __cplusplus
}
#endif
#endif