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
#include"XFuncCodeMap.h"
typedef bool (*XRecvValidCb)(XDataFrameComm* comm,const XByteArray* data);//接收验证回调(校验)
typedef void (*XSendValidCb)(XDataFrameComm* comm, XByteArray* data);//发送验证回调(添加)
typedef bool(*GetFuncCodeCb)(XDataFrameComm* comm, XByteArray* data,void* code);//从数据帧中获取功能码
#define XDATAFRAMECOMM_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XDataFrameComm))       //XDataFrameComm虚函数表大小
XCLASS_DEFINE_BEGING(XDataFrameComm)
XCLASS_DEFINE_ENUM(XDataFrameComm, SendFrameFSM) = XCLASS_VTABLE_GET_SIZE(XCommunicatorBase),
XCLASS_DEFINE_ENUM(XDataFrameComm, RecvFrameFSM),
XCLASS_DEFINE_ENUM(XDataFrameComm, SetCommMode),
XCLASS_DEFINE_ENUM(XDataFrameComm, SetFrameEndType),
XCLASS_DEFINE_ENUM(XDataFrameComm, SendData),
XCLASS_DEFINE_ENUM(XDataFrameComm, AddSendDataPeriodic),
XCLASS_DEFINE_ENUM(XDataFrameComm, SetRecvValidCRC16),
XCLASS_DEFINE_ENUM(XDataFrameComm, SetSendValidCRC16),
XCLASS_DEFINE_ENUM(XDataFrameComm, SetRecvFrameHead),
XCLASS_DEFINE_ENUM(XDataFrameComm, SetRecvFrameTail),
XCLASS_DEFINE_ENUM(XDataFrameComm, SetSendFrameHead),
XCLASS_DEFINE_ENUM(XDataFrameComm, SetSendFrameTail),
XCLASS_DEFINE_ENUM(XDataFrameComm, RemoveSendDataPeriodic),
XCLASS_DEFINE_END(XDataFrameComm)

//数据帧通信类
typedef struct XDataFrameComm
{
	XCommunicatorBase m_class;//继承类
    XDFC_State     m_state;//状态
    XDFC_CommMode  m_commMode;//通信模式
    XDFC_FrameEndType m_frameEndMode;//帧结束模式
    XDFC_SendMode     m_sendMode;//发送模式

    XDFC_RcvState m_eRcvState;// 接收状态机
    XDFC_SndState m_eSndState; // 发送状态机

    XQueueBase* m_sendFrameQueue;//发送队列(XCircularQueue<XByteArray*>)
    XListBase* m_periodicSendList;//定期发送数据链表

    XFuncCodeMap* m_funcCodeMap;//功能码映射
    GetFuncCodeCb m_getFuncCode;//获取功能码回调

    size_t m_sentBytes;//已发送字节计数
    XByteArray* m_sendFrameHead;//帧头
    XByteArray* m_sendFrameTail;//帧尾
    XTimer* m_timerRecvExpired;//检测帧接收超时
    XTimer* m_timerSendExpired;//检测发送帧到期
    XByteArray* m_recvFrameHead;//帧头
    XByteArray* m_recvFrameTail;//帧尾
    XRecvValidCb m_recvValidCb;//接收验证回调
    XSendValidCb m_sendValidCb;//发送添加验证回调
}XDataFrameComm;
XVtable* XDataFrameComm_class_init();
XDataFrameComm* XDataFrameComm_create(XIODevice* io);
void XDataFrameComm_init(XDataFrameComm* comm,XIODevice* io);
XDFC_ErrorCode XDataFrameComm_setCommMode_base(XDataFrameComm* comm, XDFC_CommMode mode);
XDFC_ErrorCode XDataFrameComm_setFrameEndType_base(XDataFrameComm* comm, XDFC_FrameEndType mode);
XDFC_ErrorCode XDataFrameComm_setSendMode(XDataFrameComm* comm, XDFC_SendMode mode);
//发送数据
XDFC_ErrorCode XDataFrameComm_sendData_base(XDataFrameComm* comm, XByteArray* data);
//发送字符串
XDFC_ErrorCode XDataFrameComm_sendText(XDataFrameComm* comm, bool appendNull, const char* str);
XDFC_ErrorCode XDataFrameComm_sendTextFmt(XDataFrameComm* comm, bool appendNull, const char* format, ...);
//定期发送
XHandle XDataFrameComm_addPeriodicSendData_base(XDataFrameComm* comm, XByteArray* data, uint32_t time);
XHandle XDataFrameComm_addPeriodicSendText(XDataFrameComm* comm, bool appendNull, uint32_t time, const char* str);
XHandle XDataFrameComm_addPeriodicSendTextFmt(XDataFrameComm* comm, bool appendNull, uint32_t time, const char* format, ...);
bool  XDataFrameComm_removePeriodicSendData_base(XDataFrameComm* comm, XHandle handle);
/*帧头和帧尾*/
void XDataFrameComm_setRecvFrameHead_base(XDataFrameComm* comm,const uint8_t* data,uint8_t dataSize);
void XDataFrameComm_setRecvFrameTail_base(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize);
void XDataFrameComm_setSendFrameHead_base(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize);
void XDataFrameComm_setSendFrameTail_base(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize);
/*校验*/
void XDataFrameComm_setRecvValidCb(XDataFrameComm* comm, XRecvValidCb cb);//接收验证数据 回调
void XDataFrameComm_setSendValidCb(XDataFrameComm* comm, XSendValidCb cb);//发送数据添加验证 回调
void XDataFrameComm_setRecvValidCRC16_base(XDataFrameComm* comm, bool enableCRC16);//接收验证数据使用CRC16，小端添加在数据末尾帧尾前
void XDataFrameComm_setSendValidCRC16_base(XDataFrameComm* comm, bool enableCRC16);//发送数据添加验证用CRC16，小端添加在数据末尾帧尾前
/*功能码*/
void XDataFrameComm_funcCodeMap_create(XDataFrameComm* comm,size_t codeSize, XCompare codeCompare);
void XDataFrameComm_addFuncCode(XDataFrameComm* comm, void* funcCode, XFuncCodeCb cb,void* userData);
void XDataFrameComm_removeFuncCode(XDataFrameComm* comm, void* funcCode);
void XDataFrameComm_clearFuncCode(XDataFrameComm* comm);
void XDataFrameComm_deleteFuncCode(XDataFrameComm* comm);
void XDataFrameComm_setGetFuncCodeCb(XDataFrameComm* comm, GetFuncCodeCb cb);//获取功能码回调
/*  XTimer 帧接收超时  帧发送超时 定时器   */
void XDataFrameComm_setTimerRecvExpired(XDataFrameComm* comm, XTimer*timer);
void XDataFrameComm_setTimerSendExpired(XDataFrameComm* comm, XTimer* timer);
#define XDataFrameComm_poll_base                   XCommunicatorBase_poll_base
#define XDataFrameComm_connect_base                XCommunicatorBase_connect_base
#define XDataFrameComm_disconnect_base             XCommunicatorBase_disconnect_base
#define XDataFrameComm_isConnected_base            XCommunicatorBase_isConnected_base
#define XDataFrameComm_delete_base                 XCommunicatorBase_delete_base
/*                                          信号                                          */
void* XDataFrameComm_frameReceived_signal(XDataFrameComm* comm,XByteArray* data, XAtomic_int32_t* ref_count);
/*以下是默认的回调函数*/

//默认的事件处理回调(全部事件)
void XDataFrameComm_EvnetHandCb(XEvent* event);
//接收到完整帧事件回调(默认获取的功能码在第一位 uint8_t)
void XDataFrameComm_EvnetFrame_ReceivedCb(XEvent* event);//通过调用设置事件处理函数重写
//执行功能码事件回调
void XDataFrameComm_EvnetExecuteCb(XEvent* event);//通过调用设置事件处理函数重写
/*以下是自定义的事件类型*/
bool XDataFrameComm_postEvent(XDataFrameComm* comm, XEvent* event);//向事件队列追加一个事件
typedef struct XEventRecvFrame
{
    XEvent m_class;//
    XByteArray* frame;//帧数据
    XAtomic_int32_t* ref_count;//引用次数
}XEventRecvFrame;//接收帧事件
//ref_count存在的时候frame是引用的方式，否则就是拷贝的方式
XEventRecvFrame* XEventRecvFrame_create(XObject* object, int eventCode, size_t timestamp,XByteArray* frame, XAtomic_int32_t* ref_count);
typedef struct XEventFuncCode
{
    XEventRecvFrame m_class;//
    void* funcCode;//功能码
}XEventFuncCode;//执行功能码事件
XEventFuncCode* XEventFuncCode_create(XObject* object, int eventCode, size_t timestamp, XByteArray* frame, void* funcCode);



#ifdef __cplusplus
}
#endif
#endif // XCOMMUNICATORBASE_H
