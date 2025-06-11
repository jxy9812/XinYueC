#ifndef XDATAFRAMECOMM_H
#define XDATAFRAMECOMM_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XCommunicatorBase.h"
#include"XDataFrameCommEnum.h"
typedef struct XQueueBase XQueueBase;
typedef struct XTimerBase XTimerBase;
typedef struct XEventDispatcher XEventDispatcher;
#define XDataFrameComm_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XDataFrameComm))       //XDataFrameComm虚函数表大小
XCLASS_DEFINE_BEGING(XDataFrameComm)
XCLASS_DEFINE_ENUM(XDataFrameComm, SendFrame) = XCLASS_VTABLE_GET_SIZE(XCommunicatorBase),
XCLASS_DEFINE_ENUM(XDataFrameComm, RecvFrame),
XCLASS_DEFINE_ENUM(XDataFrameComm, SetCommMode),
XCLASS_DEFINE_ENUM(XDataFrameComm, SetFrameEndType),
XCLASS_DEFINE_END(XDataFrameComm)

//数据帧通信类
typedef struct XDataFrameComm
{
	XCommunicatorBase m_parent;//继承类
    XDFC_State     m_state;//状态
    XDFC_CommMode  m_commMode;//通信模式
    XDFC_FrameEndType m_frameEndMode;//帧结束模式
    XDFC_SendMode     m_sendMode;//发送模式

    XDFC_RcvState m_eRcvState;// 接收状态机
    XDFC_SndState m_eSndState; // 发送状态机

    XQueueBase* m_sendFrameQueue;//发送队列(XCircularQueue<XVector*>)
    XQueueBase* m_recvFrameQueue;//接收帧队列(XCircularQueue<XVector*>) 后面处理执行
    XEventDispatcher* m_eventDispatcher;//事件调度器
    size_t m_sentBytes;//已发送字节计数
    XVector* m_sendFrameHead;//帧头
    XVector* m_sendFrameTail;//帧尾
    XTimerBase* m_timerT35Expired;//检测帧间隔超时
    XTimerBase* m_timerSendExpired;//检测发送帧到期
    //uint16_t m_frameRecv_timeout;//接收帧超时时间
    //uint16_t m_frameSend_timeout;//发送帧超时时间
    XVector* m_recvFrameHead;//帧头
    XVector* m_recvFrameTail;//帧尾
}XDataFrameComm;
XVtable* XDataFrameComm_class_init();
void XDataFrameComm_init(XDataFrameComm* comm,XIODeviceBase* io);
XDFC_ErrorCode XDataFrameComm_setCommMode_base(XDataFrameComm* comm, XDFC_CommMode mode);
XDFC_ErrorCode XDataFrameComm_setFrameEndType_base(XDataFrameComm* comm, XDFC_FrameEndType mode);
#define XDataFrameComm_poll_base  XCommunicatorBase_poll_base;
//发送一帧数据
//bool XDataFrameComm_sendFrame(XDataFrameComm* comm, XModbusFrame* frame);
#ifdef __cplusplus
}
#endif
#endif // XCOMMUNICATORBASE_H
