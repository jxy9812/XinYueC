#ifndef XMODBUSBASE_H
#define XMODBUSBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdint.h>
#include"XModbusEnum.h"
//使用默认的Modbus TCP端口（502）
#define MB_TCP_PORT_USE_DEFAULT 0   
//typedef struct XModbusBase
//{
//    uint8_t    address;         // Modbus 主机地址（1-247，0 为广播地址，255 保留）
//    XModbusMode mode;//模式
//    XModbusState     state;//状态
//    XIODeviceBase* ioDevice;//IO设备
//    XVector* recvBuffer;//接收缓冲区
//    XModbusFrameQueue* sendQueue;//发送队列(XCircularQueue<XModbusFrame*>)
//    XModbusFrameQueue* recvFrameQueue;//接收帧队列(XCircularQueue<XModbusFrame*>) 后面处理执行
//    XCustomQueue* eventQueue;//事件队列
//    XModbusFunctionHandlerList* funcCodeList;//功能码列表
//
//    int pendingCount;//待发送的数据量
//    XModbusRegularlySendFrameLsit* regularlySendMaster;//主站定期发送帧数据
//    /* ----------------------- 以下主站等待处理发送返回请求-------------------------------------*/
//    XVector* recvHandleMaster;//XVector<XModbusFrameDataRecvHandle>
//    /* ----------------------- 函数指针类型定义  0-------------------------------------*/
//    XModbusFrameSend     peMBFrameSendCur;     // 帧发送函数指针（发送完整 Modbus 帧）
//    XModbusFrameStart    pvMBFrameStartCur;    // 协议栈启动函数指针（初始化端口资源，如串口、定时器）
//    XModbusFrameStop     pvMBFrameStopCur;     // 协议栈停止函数指针（停止接收/发送，释放临时资源）
//    XModbusFrameReceive  peMBFrameReceiveCur;  // 帧接收函数指针（接收完整 Modbus 帧，返回帧数据）
//    XModbusFrameClose    pvMBFrameCloseCur;    // 端口关闭函数指针（可选，用于释放端口资源，如关闭串口）
//    /* ----------------------- RTU-------------------------------------*/
//MB_CALIBRATION_TIMER_SETTINGS
//    size_t calibrationTimer_current;//校准定时器用记录当前时间
//if // MB_CALIBRATION_TIMER_SETTINGS
//    XTimerBase* timer;//定时器     平台初始化
//    size_t timerOutNumber;//定时器超时次数
//    XModbusSndState eSndState;    // 发送状态机（volatile确保多线程可见）
//    XModbusRcvState eRcvState;    // 接收状态机
//    //函数
//    XModbusSerialEnable SerialEnable;//控制串口收发状态
//    XModbusFrameCBByteReceived pxMBFrameCBByteReceived;// 接收到单个字节时调用（触发接收状态机）
//    XModbusFrameCBTransmitterEmpty pxMBFrameCBTransmitterEmpty;// 发送缓冲区空时调用（触发发送状态机）
//    XModbusPortCBTimerExpired pxMBPortCBTimerExpired; // 定时器超时回调（如 RTU 的 T35 超时、ASCII 的 T1S 超时）
//}XModbusBase;
#ifdef __cplusplus
}
#endif
#endif // !XModbus_H
