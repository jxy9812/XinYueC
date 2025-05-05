#ifndef XMODBUSFRAMEDATA_RTU_H
#define XMODBUSFRAMEDATA_RTU_H
/*针对RTU的数据帧构建*/
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusConfig.h"
#if MB_RTU_ENABLED
#include<stdint.h>
typedef struct XModbusFrameData XModbusFrameData;
typedef struct XString XString;
typedef struct XVector XVector;

/*
* @brief  初始化RTU格式数据帧     主机地址(1)+功能码(1)+数据(dataSIze)+crc16(2)
* @param  frame:XModbusFrameData对象
* @param  address:从机地址
* @param  funcCode:功能码
* @param  dataSIze:功能码和CRC16之间的数据大小(字节)
* @retval
*/
void XModbusFrameDataRTU_initDataFrame(XModbusFrameData* frame, uint8_t address, uint8_t funcCode,uint16_t dataSIze);
/* ----------------------- RTU模式根据功能码构建数据帧-------------------------------------*/

/*
* @brief  构建数据帧0x03功能码请求帧   读保持寄存器.
* @param  frame:XModbusFrameData对象
* @param  address:从机地址
* @param  regAddress:寄存器地址(数据帧中的地址)
* @param  regCount:寄存器数量
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x03_request(XModbusFrameData* frame, uint8_t address, uint16_t regAddress,const uint16_t regCount);
/*
* @brief  构建数据帧0x03功能码响应帧   读保持寄存器
* @param  frame:XModbusFrameData对象
* @param  address:主机地址
* @param  regData:寄存器数据缓冲区
* @param  regCount:寄存器数量
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x03_reply(XModbusFrameData* frame, uint8_t address, uint16_t* regData, const uint16_t regCount);
/*
* @brief  构建数据帧0x04功能码请求帧   读输入寄存器.
* @param  frame:XModbusFrameData对象
* @param  address:从机地址
* @param  regAddress:寄存器地址(数据帧中的地址)
* @param  regCount:寄存器数量
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x04_request(XModbusFrameData* frame, uint8_t address, uint16_t regAddress,const uint16_t regCount);
/*
* @brief  构建数据帧0x04功能码响应帧   读输入寄存器
* @param  frame:XModbusFrameData对象
* @param  address:主机地址
* @param  regData:寄存器数据缓冲区(数据内存位置)
* @param  regCount:寄存器数量
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x04_reply(XModbusFrameData* frame, uint8_t address, uint16_t* regData,uint16_t regCount);
/*
* @brief  构建数据帧0x06功能码请求帧   写寄存器
* @param  frame:XModbusFrameData对象
* @param  address:从机地址
* @param  regAddress:寄存器地址(数据帧中的地址)
* @param  regData:指向要写入的寄存器数据的缓冲区(2字节)
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x06_request(XModbusFrameData* frame, uint8_t address, uint16_t regAddress,const uint16_t* regData);
/*
* @brief  构建数据帧0x06功能码响应帧   写寄存器(实际成功直接转发请求帧)
* @param  frame:XModbusFrameData对象
* @param  address:主机地址
* @param  regAddress:寄存器地址(数据帧中的地址)
* @param  regData:指向要写入的寄存器数据的缓冲区(2字节)
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x06_reply(XModbusFrameData* frame, uint8_t address, uint16_t regAddress,const uint16_t* regData);
/*
* @brief  构建数据帧0x10功能码请求帧   写多个寄存器
* @param  frame:XModbusFrameData对象
* @param  address:从机地址
* @param  regAddress:寄存器起始地址(数据帧中的地址)
* @param  regCount:要写入的寄存器数量
* @param  regData:指向要写入的寄存器数据的缓冲区(大小不小于regCount*2 否则会出错)传入数据
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x10_request(XModbusFrameData* frame, uint8_t address, uint16_t regAddress,uint16_t regCount, uint16_t* regData);
/*
* @brief  构建数据帧0x10功能码响应帧   写多个寄存器
* @param  frame:XModbusFrameData对象
* @param  address:主机地址
* @param  regAddress:寄存器地址(数据帧中的地址)
* @param  regCount:写入的寄存器数量
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x10_reply(XModbusFrameData* frame, uint8_t address, uint16_t regAddress, uint16_t regCount);
/* ----------------------- RTU模式解析数据帧-------------------------------------*/
//将一个数据解析成RTU帧
void XModbusFrameDataRTU_parseData(XModbusFrameData* frame, XVector* data);
//解析RTU模式下数据库帧中主机地址
uint8_t XModbusFrameDataRTU_parseAddress(XModbusFrameData* frame);
//解析RTU模式下数据库帧中功能码
uint8_t XModbusFrameDataRTU_parseFuncCode(XModbusFrameData* frame);
//转16进制显示
XString* XModbusFrameDataRTU_to16HexString(XModbusFrameData* frame); 

#endif // MB_RTU_ENABLED

#ifdef __cplusplus
}
#endif
#endif