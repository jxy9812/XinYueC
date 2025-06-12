#ifndef XDATAFRAMECOMM_H
#define XDATAFRAMECOMM_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XCommunicatorBase.h"
#include"XDataFrameCommEnum.h"
#include"XEvent.h"
typedef struct XQueueBase XQueueBase;
typedef struct XTimerBase XTimerBase;
typedef struct XString XString;
typedef struct XEventDispatcher XEventDispatcher;
typedef bool (*XRecvValidCb)(const XVector* data);//接收验证回调(校验)
typedef void (*XSendValidCb)(XVector* data);//发送验证回调(添加)
#define XDataFrameComm_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XDataFrameComm))       //XDataFrameComm虚函数表大小
XCLASS_DEFINE_BEGING(XDataFrameComm)
XCLASS_DEFINE_ENUM(XDataFrameComm, SendFrameFSM) = XCLASS_VTABLE_GET_SIZE(XCommunicatorBase),
XCLASS_DEFINE_ENUM(XDataFrameComm, RecvFrameFSM),
XCLASS_DEFINE_ENUM(XDataFrameComm, SetCommMode),
XCLASS_DEFINE_ENUM(XDataFrameComm, SetFrameEndType),
XCLASS_DEFINE_ENUM(XDataFrameComm, SendData),
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
    //XQueueBase* m_recvFrameQueue;//接收帧队列(XCircularQueue<XVector*>) 后面处理执行
    XEventDispatcher* m_eventDispatcher;//事件调度器
    size_t m_sentBytes;//已发送字节计数
    XVector* m_sendFrameHead;//帧头
    XVector* m_sendFrameTail;//帧尾
    XTimerBase* m_timerRecvExpired;//检测帧接收超时
    XTimerBase* m_timerSendExpired;//检测发送帧到期
    //uint16_t m_frameRecv_timeout;//接收帧超时时间
    //uint16_t m_frameSend_timeout;//发送帧超时时间
    XVector* m_recvFrameHead;//帧头
    XVector* m_recvFrameTail;//帧尾
    XRecvValidCb m_recvValidCb;//接收验证回调
    XSendValidCb m_sendValidCb;//发送添加验证回调
}XDataFrameComm;
XVtable* XDataFrameComm_class_init();
XDataFrameComm* XDataFrameComm_create(XIODeviceBase* io);
void XDataFrameComm_init(XDataFrameComm* comm,XIODeviceBase* io);
XDFC_ErrorCode XDataFrameComm_setCommMode_base(XDataFrameComm* comm, XDFC_CommMode mode);
XDFC_ErrorCode XDataFrameComm_setFrameEndType_base(XDataFrameComm* comm, XDFC_FrameEndType mode);
//发送数据
XDFC_ErrorCode XDataFrameComm_sendData_base(XDataFrameComm* comm, XVector* data);
//发送字符串
XDFC_ErrorCode XDataFrameComm_sendString(XDataFrameComm* comm, const char* str, bool appendNull);
void XDataFrameComm_setRecvFrameHead(XDataFrameComm* comm,const uint8_t* data,uint8_t dataSize);
void XDataFrameComm_setRecvFrameTail(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize);
void XDataFrameComm_setSendFrameHead(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize);
void XDataFrameComm_setSendFrameTail(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize);
void XDataFrameComm_setRecvValidCb(XDataFrameComm* comm, XRecvValidCb cb);//接收验证数据
void XDataFrameComm_setSendValidCb(XDataFrameComm* comm, XSendValidCb cb);//发送数据添加验证


#define XDataFrameComm_poll_base                   XCommunicatorBase_poll_base
#define XDataFrameComm_connect_base                XCommunicatorBase_connect_base
#define XDataFrameComm_disconnect_base             XCommunicatorBase_disconnect_base
#define XDataFrameComm_isConnected_base            XCommunicatorBase_isConnected_base


//默认的事件处理回调
void XDataFrameComm_EvnetHandCb(XEventMin* event);
typedef struct XEventRecvFrame
{
    XEventMin m_parent;//
    XVector* m_frame;//帧数据
}XEventRecvFrame;//接收帧事件

XEventRecvFrame* XEventRecvFrame_create(int code, size_t timestamp,XVector* frame);
#ifdef __cplusplus
}
#endif
#endif // XCOMMUNICATORBASE_H
