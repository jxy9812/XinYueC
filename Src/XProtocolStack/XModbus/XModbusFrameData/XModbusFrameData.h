#ifndef XMODBUSFRAMEQUEUE_H
#define XMODBUSFRAMEQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusConfig.h"
#include"XString.h"
#include"XModbusEnum.h"
#if MB_RTU_ENABLED
#include"XModbusFrameData_RTU.h"
#endif
typedef struct XQueue XQueue;
typedef XQueue XModbusFrameQueue;
//帧接收队列
typedef struct XModbus XModbus;
typedef struct XModbusFrameData XModbusFrameData;
typedef void (*XModbusFrameDataRecvHandCallFunc)(XModbus* modbus, XModbusFrameData* dataFrame);
//接收回调方法
typedef struct XModbusFrameDataRecvHandle
{
    XModbusFrameDataRecvHandCallFunc pRecvHandCallFunc;//当前回调函数
    void* userData;//用户数据
    uint16_t waitAddressCode;//等待操作的地址和功能码
}XModbusFrameDataRecvHandle;

typedef struct XModbusFrameData
{
    uint8_t address;//地址
    uint8_t funcCode;//功能码
    XModbusMode mode;//modbus的模式
    XVector* dataFrame;//数据帧
    void* data;//用来存放解析后的数据 结构体
    XModbusFrameDataRecvHandle* recvHandle;//接收处理   主站才有
}XModbusFrameData;


XModbusFrameQueue* XModbusFrameQueue_new();
void XModbusFrameQueue_push(XModbusFrameQueue* queue, XModbusFrameData* frame);
XModbusFrameData* XModbusFrameQueue_top(XModbusFrameQueue* queue);
bool XModbusFrameQueue_empty(XModbusFrameQueue* queue);
void XModbusFrameQueue_pop(XModbusFrameQueue* queue);
void XModbusFrameQueue_clear(XModbusFrameQueue* queue);
void XModbusFrameQueue_free(XModbusFrameQueue* queue);

XModbusFrameData* XModbusFrameData_new();
XModbusFrameData* XModbusFrameData_newRecvHandle();
void XModbusFrameData_free(XModbusFrameData* frame);
#if MB_RTU_ENABLED

#endif

/* ----------------------- XModbusFrameDataRecvHandle-------------------------------------*/
//将其置为0
void XModbusFrameDataRecvHandle_setZero(XModbusFrameDataRecvHandle* handle);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFrameRecvQueue_H
