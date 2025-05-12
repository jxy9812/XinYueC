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
XModbusCoilsDiscHandler* XModbusCoilsDiscHandler_new(uint16_t count);
void XModbusCoilsDiscHandler_free(XModbusCoilsDiscHandler* pRegHandler);
//写入线圈或离散
bool XModbusCoilsDiscHandler_write(XModbusCoilsDiscHandler* pRegHandler, uint16_t address, uint16_t count, const char* writeArray);
// 读取线圈或离散
bool XModbusCoilsDiscHandler_read(XModbusCoilsDiscHandler* pRegHandler, uint16_t address, uint16_t count, char* readArray, uint16_t readArraySize);
//获取指定地址寄存器的缓冲区地址
uint16_t* XModbusCoilsDiscHandler_at(XModbusCoilsDiscHandler* pRegHandler, uint16_t regAddress);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFuncCode_H
