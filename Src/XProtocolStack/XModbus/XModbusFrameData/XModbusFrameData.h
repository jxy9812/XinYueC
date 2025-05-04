#ifndef XMODBUSFRAMEQUEUE_H
#define XMODBUSFRAMEQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusPort.h"
#include"XString.h"
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
void XModbusFrameQueue_push(XModbusFrameQueue* queue, XModbusFrameData*dataFrame);
XModbusFrameData* XModbusFrameQueue_top(XModbusFrameQueue* queue);
bool XModbusFrameQueue_empty(XModbusFrameQueue* queue);
void XModbusFrameQueue_pop(XModbusFrameQueue* queue);
void XModbusFrameQueue_clear(XModbusFrameQueue* queue);
void XModbusFrameQueue_free(XModbusFrameQueue* queue);

XModbusFrameData* XModbusFrameData_new();
XModbusFrameData* XModbusFrameData_newRecvHandle();
void XModbusFrameData_free(XModbusFrameData* dataFrame);

//构建数据帧
void XModbusFrameData_setRtuDataFrame(XModbusFrameData* dataFrame,UCHAR address, const UCHAR* pPduframe, USHORT pduLength);
//构建数据帧0x06功能码
void XModbusFrameData_setRtuDataFrame_0x06_request(XModbusFrameData* dataFrame, UCHAR address, UCHAR funcCode, uint16_t regAddress, const uint16_t* regData);
//设置RTU模式下的数据+解析
void XModbusFrameData_setRtuData(XModbusFrameData* dataFrame, XVector*data);
//获取RTU模式下的主机地址
UCHAR XModbusFrameData_getRtuAddress(XModbusFrameData* dataFrame);
//获取功能码
UCHAR XModbusFrameData_getRtuFuncCode(XModbusFrameData* dataFrame);
//转16进制显示
XString* XModbusFrameData_to16HexString(XModbusFrameData* dataFrame);

//将其置为0
void XModbusFrameDataRecvHandle_setZero(XModbusFrameDataRecvHandle* handle);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFrameRecvQueue_H
