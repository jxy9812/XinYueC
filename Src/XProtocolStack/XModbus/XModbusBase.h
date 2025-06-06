#ifndef XMODBUSBASE_H
#define XMODBUSBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdint.h>
#include"XModbusEnum.h"
typedef struct XQueueBase XQueueBase; 
typedef struct XModbusFunctionHandler XModbusFunctionHandler; 
typedef struct XModbusFrame XModbusFrame;
typedef struct XVector XVector;
typedef struct XListBase XListBase;
typedef struct XTimerBase XTimerBase;
#include"XCommunicatorBase.h"
//使用默认的Modbus TCP端口（502）
#define MB_TCP_PORT_USE_DEFAULT 0
#define XMODBUSBASE_VTABLE_SIZE		(XCOMMUNICATORBASE_VTABLE_SIZE+4)       //XCommunicatorBase虚函数表大小
enum XModbusBaseVtableEnum
{
    EXModbusBase_SendFrame = XCOMMUNICATORBASE_VTABLE_SIZE,
    EXModbusBase_RecvFrame,
    EXModbusBase_ReceiveFSM,//接收数据
    EXModbusBase_TransmitFSM,//发送数据
};
typedef struct XModbusBase
{
	XCommunicatorBase m_parent;
    uint8_t    m_address;         // Modbus 主机地址（1-247，0 为广播地址，255 保留）
    XModbusMode m_mode;//模式
    XModbusState     m_state;//状态
    XQueueBase* m_sendQueue;//发送队列(XCircularQueue<XModbusFrame*>)
    XQueueBase* m_recvFrameQueue;//接收帧队列(XCircularQueue<XModbusFrame*>) 后面处理执行
    XQueueBase* m_eventQueue;//事件队列
    XVector* m_funcCodeList;//功能码列表
#if !MB_IS_COMP_SEND_FRAME
    int m_pendingCount;//待发送的数据量
#endif
    XListBase* m_regularlySendMaster;//主站定期发送帧数据
//    /* ----------------------- 以下主站等待处理发送返回请求-------------------------------------*/
    XVector* m_recvHandleMaster;//XVector<XModbusFrameDataRecvHandle>
    
//    /* ----------------------- 函数指针类型定义  0-------------------------------------*/
//    XModbusFrameSend     peMBFrameSendCur;     // 帧发送函数指针（发送完整 Modbus 帧）
//    XModbusFrameStart    pvMBFrameStartCur;    // 协议栈启动函数指针（初始化端口资源，如串口、定时器）
//    XModbusFrameStop     pvMBFrameStopCur;     // 协议栈停止函数指针（停止接收/发送，释放临时资源）
//    XModbusFrameReceive  peMBFrameReceiveCur;  // 帧接收函数指针（接收完整 Modbus 帧，返回帧数据）
//    XModbusFrameClose    pvMBFrameCloseCur;    // 端口关闭函数指针（可选，用于释放端口资源，如关闭串口）
//    /* ----------------------- RTU-------------------------------------*/
    XModbusSndState m_eSndState;    // 发送状态机（volatile确保多线程可见）
    XModbusRcvState m_eRcvState;    // 接收状态机
//    //函数
//    XModbusSerialEnable SerialEnable;//控制串口收发状态
//    XModbusFrameCBByteReceived pxMBFrameCBByteReceived;// 接收到单个字节时调用（触发接收状态机）
//    XModbusFrameCBTransmitterEmpty pxMBFrameCBTransmitterEmpty;// 发送缓冲区空时调用（触发发送状态机）
}XModbusBase;
XVtable* XModbusBase_class_init();
void XModbusBase_init(XModbusBase* modbus);
void XModbusBase_setAddress(XModbusBase* modbus, uint8_t address);
void XModbusBase_setMode(XModbusBase* modbus, XModbusMode mode);
#define XModbusBase_connect_base                XCommunicatorBase_connect_base
#define XModbusBase_disconnect_base             XCommunicatorBase_disconnect_base
#define XModbusBase_isConnected_base            XCommunicatorBase_isConnected_base
//当前是主站吗
bool XModbusBase_isMaster(XModbusBase* modbus);
//发送一帧数据
XModbusErrorCode XModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frame);
//主站定期发送
XTimerBase* XModbusBase_sendFrameRegularlyMaster(XModbusBase* modbus, XModbusFrame* frame, uint32_t time);
//设置功能码处理函数
XModbusErrorCode XModbusBase_setFunctionHandler(XModbusBase* modbus, XModbusFunctionHandler* FunctionHandler);

//发送一个事件
bool XModbusBase_sendEvent(XModbusBase* modbus, XModbusEventType event);
//轮询处理
#define XModbusBase_poll_base XCommunicatorBase_poll_base

#ifdef __cplusplus
}
#endif
#endif // !XModbus_H
