#ifndef XMODBUSFRAMEQUEUE_H
#define XMODBUSFRAMEQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusPort.h"
#include"XVector.h"
//帧接收队列
typedef struct XModbusFrameData
{
    UCHAR address;
    XVector* dataFrame;
    USHORT pduLength;//功能码+数据//不包含CRC
    UCHAR* pPduFramePos;//pdu的指针
    USHORT crc16;
}XModbusFrameData;
typedef struct XQueue XQueue;
typedef XQueue XModbusFrameQueue;

XModbusFrameQueue* XModbusFrameQueue_new();
void XModbusFrameQueue_push(XModbusFrameQueue* queue, XModbusFrameData*dataFrame);
XModbusFrameData* XModbusFrameQueue_Top(XModbusFrameQueue* queue);
bool XModbusFrameQueue_empty(XModbusFrameQueue* queue);
void XModbusFrameQueue_pop(XModbusFrameQueue* queue);
void XModbusFrameQueue_clear(XModbusFrameQueue* queue);
void XModbusFrameQueue_free(XModbusFrameQueue* queue);

XModbusFrameData XModbusFrameData_new();
//设置数据帧
void XModbusFrameData_setRtuDataFrame(XModbusFrameData* dataFrame,UCHAR address, const UCHAR* pPduframe, USHORT pduLength);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFrameRecvQueue_H
