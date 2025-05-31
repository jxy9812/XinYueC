#ifndef XMODBUSRTU_H
#define XMODBUSRTU_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include "XModbus.h"
#include "XTimerBase.h"

typedef struct XModbusFrame XModbusFrame;
    /* ----------------------- 常量定义 -----------------------------------*/
#define MB_SER_PDU_SIZE_MIN     4       // Modbus RTU帧最小长度（地址+功能码+数据+CRC=4字节）
#define MB_SER_PDU_SIZE_MAX     256     // Modbus RTU帧最大长度（256字节，含CRC）
#define MB_SER_PDU_SIZE_CRC     2       // CRC校验字段长度（2字节）
#define MB_SER_PDU_ADDR_OFF     0       // 从机地址在帧中的偏移（第0字节）
#define MB_SER_PDU_PDU_OFF      1       // Modbus PDU在帧中的偏移（地址后第1字节开始）


typedef struct XModbusRtu XModbusRtu;
//typedef struct XModbusRtu
//
//}XModbusRtu;
//typedef struct XModbusRtu
//{
//    XModbus object;
//    bool xRxEnable;//接收
//    bool xTxEnable;//发送
//    XTimerBase* timer;//定时器
//    XModbusSndState eSndState;    // 发送状态机（volatile确保多线程可见）
//    XModbusRcvState eRcvState;    // 接收状态机
////函数
//    XModbusSerialEnable SerialEnable;
//    XModbusFrameCBByteReceived FrameCBByteReceived;
//    XModbusFrameCBTransmitterEmpty FrameCBTransmitterEmpty;
//    XModbusPortCBTimerExpired PortCBTimerExpired;
//}XModbusRtu;

//modbusRTU初始化 不是给用户调用的 
XModbusErrorCode  XModbusRtuInit(XModbus* modbus, XModbusMode mode, XModbus_PortFunc* func, uint8_t address, uint8_t port, uint32_t baudRate, XModbusParity parity);
/*!
 * @brief 启动Modbus RTU接收和发送功能
 *
 * 激活RTU模式的接收状态机和定时器，开始监听总线数据。
 * 通常在eMBEnable()中被调用，无需直接调用。
 */
void            XModbusRtuStart(XModbus* modbus);

/*!
 * @brief 停止Modbus RTU通信
 *
 * 禁用RTU模式的接收/发送功能，关闭底层硬件资源（如串口）。
 * 通常在eMBDisable()中被调用，无需直接调用。
 */
void            XModbusRtuStop(XModbus* modbus);
/*!
 * @brief 接收Modbus RTU帧
 *
 * 从串口读取数据并解析完整的RTU帧，包括从站地址、功能码、数据和CRC校验。
 *
 * @param[out] pucRcvAddress 存储接收到的从站地址（0表示广播地址）
 * @param[out] pucFrame 指向接收帧数据缓冲区（包含从站地址到数据字段，不包含CRC）
 * @param[out] pusLength 接收帧的有效数据长度（单位：字节，包含从站地址和数据字段）
 *
 * @return 接收结果错误码：
 *         - MB_ENOERR：成功接收有效帧
 *         - MB_EIO：底层I/O错误（如串口读取失败）
 *         - MB_ETIMEDOUT：接收超时（超过T35定时器时间）
 */
XModbusErrorCode XModbusRtuReceive(XModbus* modbus, XModbusFrame* frameData);
/*!
 * @brief 发送Modbus RTU帧
 *
 * 将数据帧（包含从站地址、功能码、数据）添加CRC校验后通过串口发送。
 *
 * @param slaveAddress 目标从站地址（广播地址0时所有从站响应）
 * @param pucFrame 待发送的数据帧（包含从站地址、功能码、数据字段，不包含CRC）
 * @param usLength 待发送数据帧的长度（单位：字节，包含从站地址和数据字段）
 *
 * @return 发送结果错误码：
 *         - MB_ENOERR：成功发送
 *         - MB_EIO：底层I/O错误（如串口发送失败）
 */
XModbusErrorCode    XModbusRtuSend(XModbus* modbus, XModbusFrame* frameData);

/*!
  * @brief RTU接收有限状态机处理函数
  *
  * 实现RTU接收状态机逻辑，处理字节接收、帧组装和CRC校验。
  * 由协议栈主循环（eMBPoll()）自动调用，无需手动调用。
  *
  * @return BOOL 状态机是否完成当前状态处理（通常用于任务调度）
  */
bool           XModbusRtuReceiveFSM(XModbus* modbus);

/*!
 * @brief RTU发送有限状态机处理函数
 *
 * 实现RTU发送状态机逻辑，处理数据帧的CRC添加和串口发送。
 * 由协议栈主循环（eMBPoll()）自动调用，无需手动调用。
 *
 * @return BOOL 状态机是否完成当前状态处理（通常用于任务调度）
 */
bool            XModbusRtuTransmitFSM(XModbus* modbus);

/*!
 * @brief RTU模式T35定时器超时处理函数
 *
 * T35定时器用于RTU模式的帧间超时检测，超时表示一帧数据接收完成，
 * 触发协议栈对接收帧进行CRC校验和功能码处理。
 *
 * @return BOOL 定时器超时后是否触发事件（通常返回true以通知协议栈处理帧）
 */
bool           XModbusRtuTimerT35Expired(XModbus* modbus);
#ifdef __cplusplus
}
#endif
#endif // !