#ifndef XSERIALPORT_H
#define XSERIALPORT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XIODevice.h"
#include"XDeviceSerialPort.h"
//XSerialPortDevice虚函数表
#define XSERIALPORT_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XIODevice))       //XSerialPort容器虚函数表大小
typedef struct XSerialPort 
{
    XIODevice base;
    char* portName;
    uint16_t dataTerminalReady : 1;// DTR 信号
    uint16_t requestToSend : 1;// RTS 信号
    uint16_t breakEnabled : 1; // Break 信号使能
    uint16_t isOpen : 1;// 串口已打开标志
    uint16_t readyReadTriggered : 1;//是否触发了数据可读事件
    uint16_t bytesWrittenTriggered : 1;//是否触发了数据发送完成事件
    int32_t baudRate;
    XSerialPort_DataBits dataBits;
    XSerialPort_Parity parity;
    XSerialPort_StopBits stopBits;
    XSerialPort_FlowControl flowControl;
    XSerialPort_Error error;
    int64_t readBufferSize;
} XSerialPort;
//获取类型大小,此类在不同平台大小不一样,用在继承的时候使用
size_t XSerialPort_typetSize();
/**
 * @brief 初始化XSerialPort的虚函数表
 * @return 成功返回初始化后的XVtable指针，失败返回NULL
 * @details 重载XIODeviceBase基类的虚函数（open/close/read/write等），
 *          实现XSerialPort的面向对象特性，
 *          需在程序启动时调用一次，或由XSerialPort_create()自动调用
 */
XVtable* XSerialPort_class_init();
/**
 * @brief 创建并初始化一个XSerialPort实例
 * @return 成功返回指向新分配XSerialPort对象的指针，失败返回NULL
 * @details 内部调用XSerialPort_class_init确保虚表已初始化，
 *          对象需通过XObject_destroy释放
 */
XSerialPort* XSerialPort_create_ex(XMemoryType memory);
/**
 * @brief 初始化已分配的XSerialPort结构体
 * @param port 指向待初始化的XSerialPort实例（不可为NULL）
 * @details 将成员变量置为默认值，并关联虚函数表，
 *          适用于栈上或内存池分配的对象
 */
void XSerialPort_init(XSerialPort* port);

/******************************************************************************************
 * 串口配置接口
 ******************************************************************************************/

 /**
  * @brief 设置串口设备名称
  * @param port 指向XSerialPort实例的指针（不可为NULL）
  * @param name 串口名称字符串（如"COM3"或"/dev/ttyS0"），不可为NULL
  * @details 此操作不会立即打开设备，仅记录名称供后续open使用
  */
void XSerialPort_setPortName(XSerialPort* port, const char* name);
/**
 * @brief 获取当前串口设备名称
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @return 返回当前串口名称字符串，若未设置则返回空字符串
 */
const char* XSerialPort_portName(const XSerialPort* port);

/**
 * @brief 设置串口波特率
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param baudRate 波特率数值（可使用预定义常量如XSerialPort_Baud9600）
 * @param directions 应用方向（输入、输出或双向）
 * @return 设置成功返回true，否则返回false（如设备已打开且不支持动态修改）
 */
bool XSerialPort_setBaudRate(XSerialPort* port, int32_t baudRate, XSerialPort_Direction directions);

/**
 * @brief 获取当前波特率
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param directions 查询方向（通常传XSerialPort_AllDirections）
 * @return 返回当前配置的波特率值
 */
uint32_t XSerialPort_baudRate(const XSerialPort* port, XSerialPort_Direction directions);

/**
 * @brief 设置数据位
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param dataBits 数据位数（5~8）
 * @return 设置成功返回true，否则返回false
 */
bool XSerialPort_setDataBits(XSerialPort* port, XSerialPort_DataBits dataBits);
/**
 * @brief 获取当前数据位
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @return 返回当前配置的数据位
 */
XSerialPort_DataBits XSerialPort_dataBits(const XSerialPort* port);

/**
 * @brief 设置校验方式
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param parity 校验类型（如XSerialPort_EvenParity）
 * @return 设置成功返回true，否则返回false
 */
bool XSerialPort_setParity(XSerialPort* port, XSerialPort_Parity parity);

/**
 * @brief 获取当前校验方式
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @return 返回当前配置的校验类型
 */
XSerialPort_Parity XSerialPort_parity(const XSerialPort* port);

/**
 * @brief 设置停止位
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param stopBits 停止位类型（1、1.5或2）
 * @return 设置成功返回true，否则返回false
 */
bool XSerialPort_setStopBits(XSerialPort* port, XSerialPort_StopBits stopBits);

/**
 * @brief 获取当前停止位
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @return 返回当前配置的停止位类型
 */
XSerialPort_StopBits XSerialPort_stopBits(const XSerialPort* port);

/**
 * @brief 设置流控方式
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param flowControl 流控类型
 * @return 设置成功返回true，否则返回false
 */
bool XSerialPort_setFlowControl(XSerialPort* port, XSerialPort_FlowControl flowControl);

/**
 * @brief 获取底层平台的原生串口句柄。
 * 如果平台受支持且串口已打开，则返回原生串口句柄；否则返回 -1。
 *
 * 警告：此函数仅供专家使用，请自行承担风险。此外，此函数在次要版本之间不保证兼容性。
 *
 * @param port 指向 XSerialPort 实例的指针。
 * @return 成功时返回平台特定的句柄（Windows 下为 HANDLE），失败或未打开时返回 -1。
 */
XHandle XSerialPort_handle(const XSerialPort* port);

/**
 * @brief 获取当前流控方式
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @return 返回当前配置的流控类型
 */
XSerialPort_FlowControl XSerialPort_flowControl(const XSerialPort* port);

/**
 * @brief 设置DTR（Data Terminal Ready）信号状态
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param set true表示置高，false表示置低
 * @return 操作成功返回true，否则返回false（如设备未打开）
 */
bool XSerialPort_setDataTerminalReady(XSerialPort* port, bool set);

/**
 * @brief 查询DTR信号当前状态
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @return 返回DTR当前电平状态
 */
bool XSerialPort_isDataTerminalReady(const XSerialPort* port);

/**
 * @brief 设置RTS（Request To Send）信号状态
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param set true表示置高，false表示置低
 * @return 操作成功返回true，否则返回false
 */
bool XSerialPort_setRequestToSend(XSerialPort* port, bool set);

/**
 * @brief 查询RTS信号当前状态
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @return 返回RTS当前电平状态
 */
bool XSerialPort_isRequestToSend(const XSerialPort* port);

/**
 * @brief 启用或禁用Break信号（持续发送逻辑0）
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param enable true启用Break，false恢复正常传输
 * @return 操作成功返回true，否则返回false
 */
bool XSerialPort_setBreakEnabled(XSerialPort* port, bool enable);

/**
 * @brief 查询Break信号是否启用
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @return 返回Break当前使能状态
 */
bool XSerialPort_isBreakEnabled(const XSerialPort* port);

/******************************************************************************************
 * 状态与错误查询
 ******************************************************************************************/

 /**
  * @brief 获取当前引脚信号状态（如CTS、DSR等）
  * @param port 指向XSerialPort实例的指针（不可为NULL）
  * @return 返回各控制线当前状态的位掩码（XSerialPort_PinoutSignal组合）
  * @note 并非所有平台都支持全部信号，未支持的位将返回0
  */
XSerialPort_PinoutSignal XSerialPort_pinoutSignals(const XSerialPort* port);

/**
 * @brief 获取最近发生的错误类型
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @return 返回错误码，XSerialPort_NoError表示无错误
 */
XSerialPort_Error XSerialPort_error(const XSerialPort* port);

/**
 * @brief 清除当前错误状态
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @details 将内部错误码重置为XSerialPort_NoError
 */
void XSerialPort_clearError(XSerialPort* port);

/*以下是API*/
#define XSerialPort_open_base                                       XIODevice_open_base
#define XSerialPort_close_base                                      XIODevice_close_base
#define XSerialPort_delete_base                                     XIODevice_deleteLater
#define XSerialPort_write_base                                      XIODevice_write_1
#define XSerialPort_read_base                                       XIODevice_read_1
#define XSerialPort_bytesAvailable_base                             XIODevice_bytesAvailable_base
#define XSerialPort_bytesToWrite_base                               XIODevice_bytesToWrite_base
#define XSerialPort_isOpen                                          XIODevice_isOpen
#define XSerialPort_poll_base                                       XIODevice_poll_base

int64_t XSerialPort_readBufferSize(const XSerialPort* port);
void XSerialPort_setReadBufferSize(XSerialPort* port, int64_t size);

bool XSerialPort_flush(XSerialPort* port);
bool XSerialPort_clear(XSerialPort* port, XSerialPort_Direction directions);
/******************************************************************************************
 * 信号触发（兼容你的事件系统）
 ******************************************************************************************/
 /**
  * @brief 触发错误发生信号
  * @param port 指向XSerialPort实例的指针（不可为NULL）
  * @param error 错误类型
  * @return 返回信号句柄（可用于连接回调），具体类型由你的事件系统定义
  */
void* XSerialPort_errorOccurred_signal(XSerialPort* port, XSerialPort_Error error);

/**
 * @brief 触发波特率变更信号
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param baudRate 新波特率值
 * @param dir 变更方向
 * @return 返回信号句柄
 */
void* XSerialPort_baudRateChanged_signal(XSerialPort* port, uint32_t baudRate, XSerialPort_Direction dir);

/**
 * @brief 触发数据位变更信号
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param bits 新数据位
 * @return 返回信号句柄
 */
void* XSerialPort_dataBitsChanged_signal(XSerialPort* port, XSerialPort_DataBits bits);

/**
 * @brief 触发校验位变更信号
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param parity 新校验类型
 * @return 返回信号句柄
 */
void* XSerialPort_parityChanged_signal(XSerialPort* port, XSerialPort_Parity parity);

/**
 * @brief 触发停止位变更信号
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param bits 新停止位类型
 * @return 返回信号句柄
 */
void* XSerialPort_stopBitsChanged_signal(XSerialPort* port, XSerialPort_StopBits bits);

/**
 * @brief 触发流控方式变更信号
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param control 新流控类型
 * @return 返回信号句柄
 */
void* XSerialPort_flowControlChanged_signal(XSerialPort* port, XSerialPort_FlowControl control);

/**
 * @brief 触发DTR状态变更信号
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param set 新DTR状态
 * @return 返回信号句柄
 */
void* XSerialPort_dataTerminalReadyChanged_signal(XSerialPort* port, bool set);

/**
 * @brief 触发RTS状态变更信号
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param set 新RTS状态
 * @return 返回信号句柄
 */
void* XSerialPort_requestToSendChanged_signal(XSerialPort* port, bool set);

/**
 * @brief 触发Break使能状态变更信号
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param enabled 新Break状态
 * @return 返回信号句柄
 */
void* XSerialPort_breakEnabledChanged_signal(XSerialPort* port, bool enabled);



#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XSerialPort_create
#define XSerialPort_create() XSerialPort_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif
