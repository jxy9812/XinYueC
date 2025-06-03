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
typedef struct XQueueBase XQueueBase;
typedef XQueueBase XModbusFrameQueue;
//帧接收队列
typedef struct XModbus XModbus;
typedef struct XModbusFrame XModbusFrame;
typedef void (*XModbusFrameDataRecvHandCallFunc)(XModbus* modbus, XModbusFrame* frameData);
/* --------------------------------- 数据类型定义------------------------------------------*/
//接收回调方法
typedef struct XModbusFrameDataRecvHandle
{
    uint16_t waitAddressCode;//等待操作的地址和功能码
    XModbusFrameDataRecvHandCallFunc pRecvHandCallFunc;//当前回调函数
    size_t timeout;//超时时间
    void* userData;//用户数据
}XModbusFrameDataRecvHandle;

//Modbus一帧数据处理
typedef struct XModbusFrame
{
    XModbusMode mode;//modbus的模式
    XVector* frameData;//帧数据
    void* data;//用来存放解析后的数据 结构体
    XModbusFrameDataRecvHandle* recvHandle;//接收处理   主站才有
}XModbusFrame;

/* --------------------------------- XModbusFrameQueue 方法------------------------------------------*/
XModbusFrameQueue* XModbusFrameQueue_new(size_t count);
bool XModbusFrameQueue_push(XModbusFrameQueue* queue, XModbusFrame* frame);
XModbusFrame* XModbusFrameQueue_top(XModbusFrameQueue* queue);
bool XModbusFrameQueue_empty(XModbusFrameQueue* queue);
void XModbusFrameQueue_pop(XModbusFrameQueue* queue);
void XModbusFrameQueue_clear(XModbusFrameQueue* queue);
void XModbusFrameQueue_free(XModbusFrameQueue* queue);
/* --------------------------------- XModbusFrame 方法------------------------------------------*/
XModbusFrame* XModbusFrame_new();
XModbusFrame* XModbusFrame_copy(XModbusFrame* frame);
XModbusFrame* XModbusFrame_newRecvHandle();
void XModbusFrame_free(XModbusFrame* frame);
//获取数据帧中主机地址
uint8_t XModbusFrame_getAddress(XModbusFrame* frame);
//获取数据帧中功能码
uint8_t XModbusFrame_getFuncCode(XModbusFrame* frame);
#if MB_RTU_ENABLED

#endif

/* ----------------------- XModbusFrameDataRecvHandle-------------------------------------*/
//将其置为0
void XModbusFrameDataRecvHandle_setZero(XModbusFrameDataRecvHandle* handle);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFrameRecvQueue_H
