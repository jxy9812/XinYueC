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
//RTU模式下的数据
typedef struct XModbusFrameDataRTU
{
    uint8_t address;//地址
    uint8_t funcCode;//功能码
    union  {
       uint16_t coilsAddress;//线圈地址
       uint16_t discAddress;//离散地址
       uint16_t regAddress;//寄存器地址
       uint8_t  error;//错误码
    };
    union {
        uint16_t coilsCount;//线圈数量
        uint16_t discCount;//离散数量
        uint16_t regCount;//寄存器数量
    };
    uint16_t crc16;//校验码
    XVector* data;//数据 ---线圈/离散/寄存器
}XModbusFrameDataRTU;
XModbusFrameDataRTU* XModbusFrameDataRTU_new();
void XModbusFrameDataRTU_free(XModbusFrameDataRTU* data);
/* -------------------------------------- 线圈与离散输入-------------------------------------*/
#define XMODBUS_COILS_STATE_ON        0xFF00//线圈打开状态
#define XMODBUS_COILS_STATE_OFF       0x0000//线圈关闭状态  
/*
* @brief  一个字节中按偏移量设置一个比特位
* @param  ByteBuff:uint8_t* 类型 指向要操作的字节
* @param  Offset:偏移量(0~7)
* @param  State:要设置的状态(0或1)
* @retval
*/
#define XMODBUS_UINT8_SET_BITS(ByteBuff,Offset,State) (*(ByteBuff)=((State<<Offset)|(*(ByteBuff))))
/*
* @brief  一个字节中按偏移量查看一个比特位
* @param  ByteBuff:uint8_t* 类型 指向要查看的字节
* @param  Offset:偏移量(0~7)
* @retval 状态(0或1)
*/
#define XMODBUS_UINT8_GET_BITS(ByteBuff,Offset) (((*(ByteBuff))>>Offset)&0x1) 
/* --------------------------------- RTU模式初始化数据帧-------------------------------------*/
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
* @brief  构建数据帧0x01功能码请求帧   读线圈状态.
* @param  frame:XModbusFrameData对象
* @param  address:从机地址
* @param  coilsAddress:线圈地址(数据帧中的地址)
* @param  coilsCount:线圈数量
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x01_request(XModbusFrameData* frame, uint8_t address, uint16_t coilsAddress, const uint16_t coilsCount);
/*
* @brief  构建数据帧0x01功能码响应帧   读线圈状态
* @param  frame:XModbusFrameData对象
* @param  address:主机地址
* @param  coilsData:线圈数据缓冲区
* @param  coilsCount:线圈数量
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x01_reply(XModbusFrameData* frame, uint8_t address, uint8_t* coilsData, const uint16_t coilsCount);

/*
* @brief  构建数据帧0x02功能码请求帧   读离散输入状态.
* @param  frame:XModbusFrameData对象
* @param  address:从机地址
* @param  discAddress:离散输入地址(数据帧中的地址)
* @param  discCount:离散输入数量
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x02_request(XModbusFrameData* frame, uint8_t address, uint16_t discAddress, const uint16_t  discCount);
/*
* @brief  构建数据帧0x01功能码响应帧   读离散输入状态
* @param  frame:XModbusFrameData对象
* @param  address:主机地址
* @param  discAddress:离散输入数据缓冲区
* @param  discCount:离散输入数量
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x02_reply(XModbusFrameData* frame, uint8_t address, uint8_t* discAddress, const uint16_t discCount);

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
* @brief  构建数据帧0x05功能码请求帧   写单个线圈
* @param  frame:XModbusFrameData对象
* @param  address:从机地址
* @param  coilsAddress:线圈地址(数据帧中的地址)
* @param  coilsState:指向要写入的线圈状态  值填 XMODBUS_COILS_STATE_ON(开) XMODBUS_COILS_STATE_OFF(关)
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x05_request(XModbusFrameData* frame, uint8_t address, uint16_t coilsAddress, uint16_t coilsState);
/*
* @brief  构建数据帧0x05功能码响应帧   写单个线圈(实际成功直接转发请求帧)
* @param  frame:XModbusFrameData对象
* @param  address:主机地址
* @param  coilsAddress:线圈地址(数据帧中的地址)
* @param  coilsState:指向要写入的线圈状态  值填 XMODBUS_COILS_STATE_ON(开) XMODBUS_COILS_STATE_OFF(关)
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x05_reply(XModbusFrameData* frame, uint8_t address, uint16_t coilsAddress, uint16_t coilsState);
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
/*
* @brief  构建数据帧0x0F功能码请求帧   写多个线圈
* @param  frame:XModbusFrameData对象
* @param  address:从机地址
* @param  coilsAddress:线圈地址(数据帧中的地址)
* @param  coilsCount:线圈数量
* @param  coilsData:指向要写入的线圈数据的缓冲区()传入数据
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x0F_request(XModbusFrameData* frame, uint8_t address, uint16_t coilsAddress, uint16_t coilsCount, uint8_t* coilsData);
/*
* @brief  构建数据帧0x0F功能码响应帧   写多个线圈
* @param  frame:XModbusFrameData对象
* @param  address:主机地址
* @param  coilsAddress:线圈地址(数据帧中的地址)
* @param  coilsCount:线圈数量
* @retval
*/
void XModbusFrameDataRTU_setDataFrame_0x0F_reply(XModbusFrameData* frame, uint8_t address, uint16_t coilsAddress, uint16_t coilsCount);
/* ----------------------- RTU模式解析数据帧-------------------------------------*/
//将一个数据解析成RTU 请求帧
void XModbusFrameDataRTU_parseData_request(XModbusFrameData* frame, XVector* frameData);
//将一个数据解析成RTU 响应帧
void XModbusFrameDataRTU_parseData_reply(XModbusFrameData* frame, XVector* frameData);
//解析RTU模式下数据帧中主机地址
uint8_t XModbusFrameDataRTU_parseAddress(XModbusFrameData* frame);
//解析RTU模式下数据帧中功能码
uint8_t XModbusFrameDataRTU_parseFuncCode(XModbusFrameData* frame);
//转16进制显示
XString* XModbusFrameDataRTU_to16HexString(XModbusFrameData* frame); 

#endif // MB_RTU_ENABLED

#ifdef __cplusplus
}
#endif
#endif