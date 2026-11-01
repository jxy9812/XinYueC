/**
 * @file       XSerialPort.h
 * @brief      串口设备类公开 API。
 * @details    对齐 Qt QSerialPort；继承 XIODevice 并重载 open/close/read/write
 *             等虚槽，平台差异经 XDevice 串口设备层（XDeviceSerialPort）适配，
 *             本模块实现不直接调用 Win32/POSIX/STM32 平台 API。
 */
#ifndef XSERIALPORT_H
#define XSERIALPORT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XIODevice.h"
#include"XDeviceSerialPort.h"
#if XDEVICE_SERIALPORT_ON
//XSerialPortDevice虚函数表
//XSerialPort 仅继承 XIODevice，未新增虚函数，重载的虚函数在 XSerialPort.c 中实现
XCLASS_DEFINE_BEGING(XSerialPort)
XCLASS_DEFINE_EXTEND_END(XSerialPort, XIODevice)
#define XSERIALPORT_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XIODevice))       //XSerialPort容器虚函数表大小
/**
 * @brief 默认接收轮询间隔（毫秒）
 * @details 串口设备层无 fd 级可读事件，XSerialPort 用该间隔的定时器把"有数据"转成 readyRead 信号。
 *          2ms 对 38400bps 的 Modbus RTU 足够（整帧 35 字节约 10ms，轮询只影响首个字节的感知延迟）。
 */
#define XSerialPort_DefaultRxPollIntervalMs 2
/**
 * @brief 串口设备类，对齐 Qt QSerialPort。
 * @details 继承 XIODevice；堆对象经 XSerialPort_create* 创建并用 XClassDelete 释放，
 *          栈对象经 XSerialPort_init 初始化并用 XClassDeinit 反初始化。
 */
typedef struct XSerialPort
{
    XIODevice base;/**< 基类子对象；第一个成员，由 XClass/XIODevice 基类机制管理，禁止手工修改 */
    char* portName;/**< 串口设备名称（如"COM3"、"/dev/ttyS0"），UTF-8 编码；由对象拥有（setPortName 时深拷贝，反初始化时释放），NULL 表示未设置，调用者不得直接修改 */
    uint16_t dataTerminalReady : 1;/**< DTR（数据终端就绪）信号电平缓存；1=置高 0=置低，仅设备打开时有意义 */
    uint16_t requestToSend : 1;/**< RTS（请求发送）信号电平缓存；1=置高 0=置低，仅设备打开时有意义 */
    uint16_t breakEnabled : 1;/**< Break 信号使能状态；1=持续发送逻辑 0，0=正常传输 */
    uint16_t isOpen : 1;/**< 串口已打开标志；1=已打开 0=未打开，仅供实现内部维护 */
    uint16_t readyReadTriggered : 1;/**< 是否触发了数据可读事件；仅供实现内部维护 */
    uint16_t bytesWrittenTriggered : 1;/**< 是否触发了数据发送完成事件；仅供实现内部维护 */
    XTimerId m_rxTimer;/**< RX 轮询定时器标识；打开期间用于把"有数据可读"转成 readyRead 信号；未启动时为 XTIMER_INVALID_ID */
    int32_t rxPollIntervalMs;/**< RX 轮询间隔（毫秒）；打开期间每拍检查一次接收缓冲是否有数据；<=0 表示关闭轮询（不发射 readyRead） */
    int32_t baudRate;/**< 波特率（单位：波特），有效范围受平台限定，默认 XSerialPort_Baud9600 */
    XSerialPort_DataBits dataBits;/**< 数据位数（5~8 位），默认 XSerialPort_Data8 */
    XSerialPort_Parity parity;/**< 校验方式，默认 XSerialPort_NoParity */
    XSerialPort_StopBits stopBits;/**< 停止位类型（1/1.5/2 位），默认 XSerialPort_OneStop */
    XSerialPort_FlowControl flowControl;/**< 流控方式，默认 XSerialPort_NoFlowControl */
    XSerialPort_Error error;/**< 最近一次操作的错误码；XSerialPort_NoError 表示无错误 */
    int64_t readBufferSize;/**< 内部读缓冲区容量（单位：字节），必须大于 0，默认 512*1024 */
} XSerialPort;
/**
 * @brief 获取XSerialPort类型大小
 * @details 此类在不同平台大小不一样，供继承该类时计算子对象偏移使用
 * @return 返回sizeof(XSerialPort)的字节数，恒为正值
 */
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
 *          对象需通过XClassDelete释放（栈对象经XSerialPort_init初始化后用XClassDeinit反初始化）
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
 * @brief 设置接收轮询间隔
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param intervalMs 轮询间隔（毫秒）；<=0 表示关闭轮询
 * @details 串口设备层（XDeviceSerialPort）在 POSIX/Windows 上是纯同步读模型，
 *          没有 fd 级可读事件通知，因此 XSerialPort 自行持有一个毫秒级定时器：
 *          每拍检查一次接收缓冲，有数据则发射 XIODevice 的 readyRead 信号。
 *          Modbus RTU 主站依赖该信号切分帧边界，必须保持开启。
 * @note 仅在串口打开前设置生效（打开后修改需重新打开串口）
 */
void XSerialPort_setRxPollInterval(XSerialPort* port, int32_t intervalMs);
/**
 * @brief 获取接收轮询间隔
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @return 返回当前轮询间隔（毫秒）
 */
int32_t XSerialPort_rxPollInterval(const XSerialPort* port);

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
/**
 * @brief 打开串口（委托 XIODevice_open_base 虚函数）
 * @param self XSerialPort 实例指针
 * @param mode 打开模式（Read/Write/ReadWrite 组合）
 * @return 成功返回 true；失败返回 false 并置 error / 发 errorOccurred 信号
 * @note [单开契约] 同一端口同一时刻只能被一个实例打开；同一 XSerialPort
 *       实例已处于打开状态时再次调用 open 直接返回 false
 *       （error = XSerialPort_OpenError，不发射 errorOccurred —— 拒绝性
 *       双开不是端口故障），不会静默重开覆盖旧 fd。
 *       内部需要重开路径（如重连）必须先显式 close 再 open。
 */
#define XSerialPort_open_base                                       XIODevice_open_base
#define XSerialPort_close_base                                      XIODevice_close_base
#define XSerialPort_write_base                                      XIODevice_write_1
#define XSerialPort_read_base                                       XIODevice_read_1
#define XSerialPort_bytesAvailable_base                             XIODevice_bytesAvailable_base
#define XSerialPort_bytesToWrite_base                               XIODevice_bytesToWrite_base
#define XSerialPort_isOpen                                          XIODevice_isOpen
#define XSerialPort_poll_base                                       XIODevice_poll_base

/**
 * @brief 获取内部读缓冲区容量
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @return 返回当前读缓冲区容量（字节）；port为NULL时返回0
 */
int64_t XSerialPort_readBufferSize(const XSerialPort* port);
/**
 * @brief 设置内部读缓冲区容量
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param size 新的缓冲区容量（字节），必须大于0
 * @details 设备已打开时尝试同步到平台层，失败时保持原容量不变
 */
void XSerialPort_setReadBufferSize(XSerialPort* port, int64_t size);

/**
 * @brief 立即写出输出缓冲区中的待发数据
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @return 刷新成功返回true；port为NULL、设备未打开或平台刷新失败返回false
 */
bool XSerialPort_flush(XSerialPort* port);
/**
 * @brief 清除指定方向上已接收/待发送的缓冲数据
 * @param port 指向XSerialPort实例的指针（不可为NULL）
 * @param directions 清除方向（输入、输出或双向），须为XSerialPort_AllDirections范围内的组合
 * @return 清除成功返回true；port为NULL、设备未打开或directions非法返回false
 */
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



#endif /* XDEVICE_SERIALPORT_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XSerialPort_create
#define XSerialPort_create() XSerialPort_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif
