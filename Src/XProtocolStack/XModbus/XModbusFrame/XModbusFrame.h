#ifndef XMODBUSFRAME_H
#define XMODBUSFRAME_H
#ifdef __cplusplus
extern "C" {
#endif
/* ----------------------- 宏定义 ------------------------------------------*/
// 协议数据单元（PDU）的最大尺寸
#define MB_PDU_SIZE_MAX     253 
// 协议数据单元（PDU）的最小尺寸，仅包含功能码
#define MB_PDU_SIZE_MIN     1   
// 功能码在协议数据单元（PDU）中的偏移量
#define MB_PDU_FUNC_OFF     0   
// 响应数据在协议数据单元（PDU）中的偏移量
#define MB_PDU_DATA_OFF     1   

#include"XModbusConfig.h"
#include"XString.h"
#include"XModbusEnum.h"
#if MB_RTU_ENABLED
#include"XModbusFrame_RTU.h"
#endif
/* ---------------------------------  类型声明------------------------------------------*/
//帧接收队列
typedef struct XModbusFrame XModbusFrame;
/* --------------------------------- 数据类型定义------------------------------------------*/
//Modbus一帧数据处理
typedef struct XModbusFrame
{
    XModbusMode mode;//modbus的模式
    XByteArray* frameData;//帧数据
    void* data;//用来存放解析后的数据 结构体
}XModbusFrame;

/* --------------------------------- XModbusFrame 方法------------------------------------------*/
XModbusFrame* XModbusFrame_create(XModbusMode mode);
XModbusFrame* XModbusFrame_copy(XModbusFrame* frame);
void XModbusFrame_setMode(XModbusFrame* frame, XModbusMode mode);
void XModbusFrame_delete(XModbusFrame* frame);
//获取数据帧中主机地址
uint8_t XModbusFrame_getAddress(XModbusFrame* frame);
//获取数据帧中功能码
uint8_t XModbusFrame_getFuncCode(XModbusFrame* frame);
//将一个数据解析
bool XModbusFrame_parseData(XModbusFrame* frame, XByteArray* frameData);
#if MB_RTU_ENABLED

#endif
#ifdef __cplusplus
}
#endif
#endif // !XModbusFrameRecvQueue_H
