#ifndef XMODBUSFRAMEQUEUE_H
#define XMODBUSFRAMEQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusConfig.h"
#include"XModbusPort.h"
#include"XString.h"
#if MB_RTU_ENABLED
#include"XModbusFrameData_RTU.h"
#endif
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
    UCHAR address;//地址
    UCHAR funcCode;//功能码
    XVector* dataFrame;//数据帧
    USHORT pduLength;//功能码+数据//不包含CRC
    UCHAR* pPduFramePos;//pdu的指针，指向 dataFrame中
    //USHORT crc16;
    
    XModbusFrameDataRecvHandle* recvHandle;//接收处理   主站才有
}XModbusFrameData;
typedef struct XQueue XQueue;
typedef XQueue XModbusFrameQueue;

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


/* ----------------------- XModbusFrameDataRecvHandle-------------------------------------*/
//将其置为0
void XModbusFrameDataRecvHandle_setZero(XModbusFrameDataRecvHandle* handle);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFrameRecvQueue_H
