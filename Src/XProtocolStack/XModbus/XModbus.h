#ifndef XMODBUS_H
#define XMODBUS_H
#ifdef __cplusplus
extern "C" {
#endif

#include"XVector.h"
#include"XQueue.h"
#include"XIODevice.h"
#include"XEventQueue.h"
#include"XModbusFrame.h"
#include"XTimer.h"
#include"XModbusFunctionHandler.h"
#include"XModbusRegisterFunc.h"
#include"XModbusRegularlySendFrame.h"
#include"XModbusEnum.h"
    /* ----------------------- 宏定义 ------------------------------------------*/

//使用默认的Modbus TCP端口（502）
#define MB_TCP_PORT_USE_DEFAULT 0   

  /* ----------------------- 类型定义 ---------------------------------------*/

typedef struct XModbus XModbus;
typedef XVector XModbusFunctionHandlerList;
/* ----------------------- 函数指针类型定义  0-------------------------------------*/
// 开始接收Modbus帧的函数指针类型
typedef void    (*XModbusFrameStart) (XModbus* modbus);
// 停止接收Modbus帧的函数指针类型
typedef void    (*XModbusFrameStop) (XModbus* modbus);
// 接收Modbus帧的函数指针类型
typedef XModbusErrorCode(*XModbusFrameReceive) (XModbus* modbus, XModbusFrame* frameData);
// 发送Modbus帧的函数指针类型
typedef XModbusErrorCode(*XModbusFrameSend) (XModbus* modbus, XModbusFrame* frameData);
// 关闭Modbus帧处理的函数指针类型
typedef void(*XModbusFrameClose) (XModbus* modbus);
/*!
 * @brief 启用/禁用串口收发
 * @param xRxEnable true=启用接收，false=禁用接收
 * @param xTxEnable true=启用发送，false=禁用发送
 */
typedef void(*XModbusSerialEnable)(XModbus* modbus, bool xRxEnable, bool xTxEnable);
/* 端口层回调函数（需平台实现，处理外部事件驱动协议栈） */
typedef bool(*XModbusFrameCBByteReceived)(XModbus* modbus);   // 接收到单个字节时调用（触发接收状态机）
typedef bool(*XModbusFrameCBTransmitterEmpty)(XModbus* modbus); // 发送缓冲区空时调用（触发发送状态机）
typedef bool(*XModbusPortCBTimerExpired)(XModbus* modbus);    // 定时器超时回调（如 RTU 的 T35 超时、ASCII 的 T1S 超时）
//初始化的函数
typedef struct XModbus_PortFunc
{
    XIODevice_PortFunc IO_Port;//io接口
    XTimer_PortFunc    timePort;
    XModbusSerialEnable SerialEnable;//控制串口收发状态  可以为NULL
    XEventQueueInit EventQueueInit;//时间队列初始化函数 不是必须可以为空使用默认的事件队列
}XModbus_PortFunc;
typedef struct XModbus
{
    uint8_t    address;         // Modbus 主机地址（1-247，0 为广播地址，255 保留）
    XModbusMode mode;//模式
    XModbusErrorCode errorCode;//错误代码
    XModbusState     state;//状态
    XIODevice* ioDevice;//IO设备
    XModbusFrameQueue* sendQueue;//发送队列(XQueue<XModbusFrame*>)
    XModbusFrameQueue* recvFrameQueue;//接收帧队列(XQueue<XModbusFrame*>) 后面处理执行
    XEventQueue* eventQueue;//事件队列
    XModbusFunctionHandlerList* funcCodeList;//功能码列表

    int sendRemaining;//当前发送的进度
    XModbusRegularlySendFrameLsit* regularlySendMaster;//主站定期发送帧数据
    /* ----------------------- 以下主站等待处理发送返回请求-------------------------------------*/
    XVector* recvHandleMaster;//XVector<XModbusFrameDataRecvHandle>
    /* ----------------------- 函数指针类型定义  0-------------------------------------*/
    XModbusFrameSend     peMBFrameSendCur;     // 帧发送函数指针（发送完整 Modbus 帧）
    XModbusFrameStart    pvMBFrameStartCur;    // 协议栈启动函数指针（初始化端口资源，如串口、定时器）
    XModbusFrameStop     pvMBFrameStopCur;     // 协议栈停止函数指针（停止接收/发送，释放临时资源）
    XModbusFrameReceive  peMBFrameReceiveCur;  // 帧接收函数指针（接收完整 Modbus 帧，返回帧数据）
    XModbusFrameClose    pvMBFrameCloseCur;    // 端口关闭函数指针（可选，用于释放端口资源，如关闭串口）
    /* ----------------------- RTU-------------------------------------*/
   // bool xRxEnable;//接收
   // bool xTxEnable;//发送
    XTimer* timer;//定时器     平台初始化
    size_t timerOutNumber;//定时器超时次数
    XModbusSndState eSndState;    // 发送状态机（volatile确保多线程可见）
    XModbusRcvState eRcvState;    // 接收状态机
    //函数
    XModbusSerialEnable SerialEnable;//控制串口收发状态
    XModbusFrameCBByteReceived pxMBFrameCBByteReceived;// 接收到单个字节时调用（触发接收状态机）
    XModbusFrameCBTransmitterEmpty pxMBFrameCBTransmitterEmpty;// 发送缓冲区空时调用（触发发送状态机）
    XModbusPortCBTimerExpired pxMBPortCBTimerExpired; // 定时器超时回调（如 RTU 的 T35 超时、ASCII 的 T1S 超时）
}XModbus;
/*
* @brief  Modbus初始化
* @param  modbus:XModbus对象指针
* @param  func:XModbus_PortFunc 初始化结构体
* @param  mode:启动模式
* @param  address:主机地址
* @param  port:端口号
* @param  baudRate:波特率
* @param  parity:奇偶校验
* @retval
*/
void XModbus_init(XModbus* modbus, XModbus_PortFunc* func,XModbusMode mode,uint8_t address,uint8_t port,uint32_t baudRate, XModbusParity parity);
/*! \ingroup modbus
 * \brief 启用Modbus协议栈
 *
 * 启用Modbus帧处理，仅在协议栈禁用状态下有效。
 *
 * \return 成功启用返回MB_ENOERR，非禁用状态返回MB_EILLSTATE
 */
XModbusErrorCode    XModbus_enable(XModbus* modbus);
/*! \ingroup modbus
 * \brief 禁用Modbus协议栈
 *
 * 停止Modbus帧处理。
 *
 * \return 成功禁用返回MB_ENOERR，非启用状态返回MB_EILLSTATE
 */
XModbusErrorCode   XModbus_disable(XModbus* modbus);
/*! \ingroup modbus
 * \brief 协议栈主轮询函数
 *
 * 需周期性调用，轮询间隔由Modbus从机超时配置决定。
 * 内部调用xMBPortEventGet()等待接收/发送状态机事件。
 *
 * \return 协议栈未启用时返回MB_EILLSTATE，正常返回MB_ENOERR
 */
XModbusErrorCode  XModbus_poll(XModbus* modbus);

//发送一帧数据
XModbusErrorCode XModbus_sendFrame(XModbus* modbus, XModbusFrame* frame);
//主站定期发送
XModbusErrorCode XModbus_sendFrameRegularlyMaster(XModbus* modbus, XModbusFrame* frame,uint32_t time);
//当前是主站吗
bool XModbus_isMaster(XModbus* modbus);
//设置功能码处理函数
XModbusErrorCode XModbus_setFunctionHandler(XModbus* modbus, XModbusFunctionHandler* FunctionHandler);
#ifdef __cplusplus
}
#endif
#endif // !XModbus_H
