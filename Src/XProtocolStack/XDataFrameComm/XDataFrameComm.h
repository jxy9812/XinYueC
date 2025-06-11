#ifndef XDATAFRAMECOMM_H
#define XDATAFRAMECOMM_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XCommunicatorBase.h"
typedef struct XQueueBase XQueueBase;
typedef struct XEventDispatcher XEventDispatcher;
#define XDataFrameComm_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XDataFrameComm))       //XDataFrameComm虚函数表大小
XCLASS_DEFINE_BEGING(XDataFrameComm)
XCLASS_DEFINE_ENUM(XDataFrameComm, SendFrame) = XCLASS_VTABLE_GET_SIZE(XCommunicatorBase),
XCLASS_DEFINE_ENUM(XDataFrameComm, RecvFrame),
XCLASS_DEFINE_END(XDataFrameComm)
typedef enum
{
    XDFC_READY,                   /*!< 启动完成事件 */
    XDFC_FRAME_RECEIVED,          /*!< 接收到完整帧事件 */
    XDFC_EXECUTE,                 /*!< 执行功能码处理事件 */
    XDFC_FRAME_SENT               /*!< 帧发送完成事件 */
}XDFC_EventType;
// 协议栈状态机（未初始化/禁用/启用）
typedef enum 
{
    XDFC_STATE_ENABLED,       // 协议栈已启用，正在处理通信（调用 eMBEnable 后）
    XDFC_STATE_DISABLED,      // 协议栈已禁用，资源未释放（可通过 eMBEnable 重新激活）
    XDFC_STATE_NOT_INITIALIZED// 协议栈未初始化（初始状态，需调用 eMBInit 初始化）
} XDFC_State;
//数据帧通信类
typedef struct XDataFrameComm
{
	XCommunicatorBase m_parent;//继承类
    XDFC_State     m_state;//状态
    XQueueBase* m_sendFrameQueue;//发送队列(XCircularQueue<XModbusFrame*>)
    XQueueBase* m_recvFrameQueue;//接收帧队列(XCircularQueue<XModbusFrame*>) 后面处理执行
    XEventDispatcher* m_eventDispatcher;//事件调度器
}XDataFrameComm;
XVtable* XDataFrameComm_class_init();
void XDataFrameComm_init(XDataFrameComm* comm,uint16_t sendQueueCount,uint16_t recvQueueCount,uint16_t eventQueueCount);
#define XDataFrameComm_poll_base  XCommunicatorBase_poll_base;

#ifdef __cplusplus
}
#endif
#endif // XCOMMUNICATORBASE_H
