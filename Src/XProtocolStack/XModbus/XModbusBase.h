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
#include"XDataFrameComm.h"
//使用默认的Modbus TCP端口（502）
#define MB_TCP_PORT_USE_DEFAULT 0
#define XMODBUSBASE_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XDataFrameComm)+4)       //XCommunicatorBase虚函数表大小
enum XModbusBaseVtableEnum
{
    EXModbusBase_SendFrame = XCLASS_VTABLE_GET_SIZE(XCommunicatorBase),
    EXModbusBase_RecvFrame,
};
typedef struct XModbusBase
{
    XDataFrameComm m_parent;
    uint8_t    m_address;         // Modbus 主机地址（1-247，0 为广播地址，255 保留）
    XModbusMode m_mode;//模式
    //XModbusState     m_state;//状态
    //XQueueBase* m_sendQueue;//发送队列(XCircularQueue<XModbusFrame*>)
    //XQueueBase* m_recvFrameQueue;//接收帧队列(XCircularQueue<XModbusFrame*>) 后面处理执行
    //XQueueBase* m_eventQueue;//事件队列
    //XVector* m_funcCodeList;//功能码列表
//#if !MB_IS_COMP_SEND_FRAME
//    int m_pendingCount;//待发送的数据量
//#endif
    //XListBase* m_regularlySendMaster;//主站定期发送帧数据
//    /* ----------------------- 以下主站等待处理发送返回请求-------------------------------------*/
    XVector* m_recvHandleMaster;//XVector<XModbusFrameDataRecvHandle>
//    /* ----------------------- RTU-------------------------------------*/
    //XModbusSndState m_eSndState;    // 发送状态机（volatile确保多线程可见）
    //XModbusRcvState m_eRcvState;    // 接收状态机
}XModbusBase;
XVtable* XModbusBase_class_init();
void XModbusBase_init(XModbusBase* modbus, XIODeviceBase* io);
void XModbusBase_setAddress(XModbusBase* modbus, uint8_t address);
void XModbusBase_setMode(XModbusBase* modbus, XModbusMode mode);
#define XModbusBase_connect_base                XCommunicatorBase_connect_base
#define XModbusBase_disconnect_base             XCommunicatorBase_disconnect_base
#define XModbusBase_isConnected_base            XCommunicatorBase_isConnected_base
//当前是主站吗
bool XModbusBase_isMaster(XModbusBase* modbus);
//发送一帧数据
XDFC_ErrorCode XModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frame);
//主站定期发送
XTimerBase* XModbusBase_sendFrameRegularlyMaster(XModbusBase* modbus, XModbusFrame* frame, uint32_t time);
//轮询处理
#define XModbusBase_poll_base XCommunicatorBase_poll_base

#ifdef __cplusplus
}
#endif
#endif // !XModbus_H
