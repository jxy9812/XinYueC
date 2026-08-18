/**
 * @file XDeviceSerialPort.h
 * @brief 统一串口设备类、打开选项、属性和控制命令。
 * @details XDeviceSerialPort 继承 XDevice。业务层只使用 XDevice_open/read/write/
 *          getProperty/setProperty/control/close；POSIX、Windows 和 STM32 实现只在
 *          各自源文件中提供内部平台钩子，不向公共头暴露原生句柄函数。
 */
#ifndef XDEVICESERIALPORT_H
#define XDEVICESERIALPORT_H

#include "XDevice.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XSERIALPORT_ON
#define XSERIALPORT_ON 1
#endif

#if defined(XSERIALPORT_USE_PLATFORM_API) && defined(XSERIALPORT_USE_STM32)
#error "XSERIALPORT_USE_PLATFORM_API and XSERIALPORT_USE_STM32 are mutually exclusive"
#endif

#if !defined(XSERIALPORT_USE_PLATFORM_API) && !defined(XSERIALPORT_USE_STM32)
#if defined(USE_STDPERIPH_DRIVER)
#define XSERIALPORT_USE_STM32 1
#else
#define XSERIALPORT_USE_PLATFORM_API 1
#endif
#endif

typedef struct XSerialPort XSerialPort;
typedef struct XRingBuffer XRingBuffer;

/** @brief 串口数据方向位组合。 */
typedef enum XSerialPort_Direction
{
    XSerialPort_Input = 1,       /**< 输入方向。 */
    XSerialPort_Output = 2,      /**< 输出方向。 */
    XSerialPort_AllDirections = XSerialPort_Input | XSerialPort_Output /**< 输入和输出方向。 */
} XSerialPort_Direction;

/** @brief 常用串口波特率；设备也接受其它正整数波特率。 */
typedef enum XSerialPort_BaudRate
{
    XSerialPort_Baud1200 = 1200,
    XSerialPort_Baud2400 = 2400,
    XSerialPort_Baud4800 = 4800,
    XSerialPort_Baud9600 = 9600,
    XSerialPort_Baud19200 = 19200,
    XSerialPort_Baud38400 = 38400,
    XSerialPort_Baud57600 = 57600,
    XSerialPort_Baud115200 = 115200
} XSerialPort_BaudRate;

/** @brief 串口数据位。 */
typedef enum XSerialPort_DataBits
{
    XSerialPort_Data5 = 5,
    XSerialPort_Data6 = 6,
    XSerialPort_Data7 = 7,
    XSerialPort_Data8 = 8,
    XSerialPort_Data9 = 9
} XSerialPort_DataBits;

/** @brief 串口校验方式。 */
typedef enum XSerialPort_Parity
{
    XSerialPort_NoParity = 0,
    XSerialPort_EvenParity = 2,
    XSerialPort_OddParity = 3,
    XSerialPort_SpaceParity = 4,
    XSerialPort_MarkParity = 5
} XSerialPort_Parity;

/** @brief 串口停止位。 */
typedef enum XSerialPort_StopBits
{
    XSerialPort__ZeroPointFive = 0,
    XSerialPort_OneStop = 1,
    XSerialPort_TwoStop = 2,
    XSerialPort_OneAndHalfStop = 3
} XSerialPort_StopBits;

/** @brief 串口流控方式。 */
typedef enum XSerialPort_FlowControl
{
    XSerialPort_NoFlowControl = 0,
    XSerialPort_HardwareControl,
    XSerialPort_SoftwareControl,
    XSerialPort_BothControl
} XSerialPort_FlowControl;

/** @brief 串口引脚信号位组合。 */
typedef enum XSerialPort_PinoutSignal
{
    XSerialPort_NoSignal = 0x00,
    XSerialPort_DataTerminalReadySignal = 0x04,
    XSerialPort_DataCarrierDetectSignal = 0x08,
    XSerialPort_DataSetReadySignal = 0x10,
    XSerialPort_RingIndicatorSignal = 0x20,
    XSerialPort_RequestToSendSignal = 0x40,
    XSerialPort_ClearToSendSignal = 0x80
} XSerialPort_PinoutSignal;

/** @brief 串口错误类型。 */
typedef enum XSerialPort_Error
{
    XSerialPort_NoError = 0,
    XSerialPort_DeviceNotFoundError,
    XSerialPort_PermissionError,
    XSerialPort_OpenError,
    XSerialPort_WriteError,
    XSerialPort_ReadError,
    XSerialPort_ResourceError,
    XSerialPort_UnsupportedOperationError,
    XSerialPort_UnknownError,
    XSerialPort_TimeoutError,
    XSerialPort_NotOpenError
} XSerialPort_Error;

/**
 * @brief 串口设备打开选项。
 * @details m_base.m_target 为串口名称；本结构必须通过
 *          XDeviceSerialPortOpenOptions_init 初始化后再覆盖所需字段。
 */
typedef struct XDeviceSerialPortOpenOptions
{
    XDeviceOpenOptions m_base;               /**< 父类打开选项。 */
    XSerialPort* m_owner;                    /**< 可选高层 XSerialPort 借用指针。 */
    const void* m_platformData;               /**< 平台配置借用指针；平台无额外配置时为 NULL。 */
    int32_t m_baudRate;                      /**< 波特率，必须大于 0。 */
    XSerialPort_DataBits m_dataBits;         /**< 数据位。 */
    XSerialPort_Parity m_parity;             /**< 校验方式。 */
    XSerialPort_StopBits m_stopBits;         /**< 停止位。 */
    XSerialPort_FlowControl m_flowControl;   /**< 流控方式。 */
    int64_t m_readBufferSize;                /**< 接收缓冲上限提示，单位字节。 */
} XDeviceSerialPortOpenOptions;

/** @brief 初始化串口打开选项为 9600-8-N-1、无流控、512 KiB 接收缓冲。 */
void XDeviceSerialPortOpenOptions_init(XDeviceSerialPortOpenOptions* options);

/** @brief 串口设备专有属性，从父类设备属性空间继续分配。 */
typedef enum XDeviceSerialPortProperty
{
    XDeviceSerialPortProperty_BaudRate = XDeviceProperty_Count
        /**< 值为 int32_t。 */,
    XDeviceSerialPortProperty_DataBits
        /**< 值为 XSerialPort_DataBits。 */,
    XDeviceSerialPortProperty_Parity
        /**< 值为 XSerialPort_Parity。 */,
    XDeviceSerialPortProperty_StopBits
        /**< 值为 XSerialPort_StopBits。 */,
    XDeviceSerialPortProperty_FlowControl
        /**< 值为 XSerialPort_FlowControl。 */,
    XDeviceSerialPortProperty_ReadBufferSize
        /**< 值为 int64_t。 */,
    XDeviceSerialPortProperty_DataTerminalReady
        /**< 值为 bool；setProperty 修改 DTR，getProperty 返回已设置状态。 */,
    XDeviceSerialPortProperty_RequestToSend
        /**< 值为 bool；setProperty 修改 RTS，硬件流控时平台可拒绝。 */,
    XDeviceSerialPortProperty_BreakEnabled
        /**< 值为 bool；setProperty 开启或关闭 Break。 */,
    XDeviceSerialPortProperty_BytesAvailable
        /**< 只读，值为 int64_t。 */,
    XDeviceSerialPortProperty_BytesToWrite
        /**< 只读，值为 int64_t。 */,
    XDeviceSerialPortProperty_PinoutSignals
        /**< 只读，值为 XSerialPort_PinoutSignal 位组合。 */,
    XDeviceSerialPortProperty_Error
        /**< 只读，值为 XSerialPort_Error。 */,
    XDeviceSerialPortProperty_Count
        /**< 串口属性数量边界，不可传给属性 API。 */
} XDeviceSerialPortProperty;

/** @brief 串口设备专有控制命令，继承全部 XDeviceCommand。 */
typedef enum XDeviceSerialPortCommand
{
    XDeviceSerialPortCommand_HandleEvent = XDeviceCommand_Count
        /**< 处理异步完成事件；in 为 XVarList(XEvent* event)，out 为 NULL。 */,
    XDeviceSerialPortCommand_Clear
        /**< 清空串口缓冲；in 为 XVarList(XSerialPort_Direction directions)，out 为 NULL。 */,
    XDeviceSerialPortCommand_ClearError
        /**< 清除串口错误；in/out 均为 NULL。 */,
    XDeviceSerialPortCommand_Count
        /**< 串口命令数量；后续子类从此值继续编号，不可传给 XDevice_control。 */
} XDeviceSerialPortCommand;

/** @brief 串口设备唯一打开上下文；平台结构必须以它为第一个成员。 */
typedef struct XDeviceSerialPortContext
{
    XDeviceContext m_base;                    /**< XDevice 上下文基类。 */
    XSerialPort* m_owner;                     /**< 高层串口对象借用指针，可为 NULL。 */
    XRingBuffer* m_readBuffer;                /**< 已完成的异步读取数据，由上下文拥有。 */
    XRingBuffer* m_writeBuffer;               /**< 等待异步写入的数据，由上下文拥有。 */
    int32_t m_baudRate;                       /**< 当前波特率。 */
    XSerialPort_DataBits m_dataBits;          /**< 当前数据位。 */
    XSerialPort_Parity m_parity;              /**< 当前校验方式。 */
    XSerialPort_StopBits m_stopBits;          /**< 当前停止位。 */
    XSerialPort_FlowControl m_flowControl;    /**< 当前流控方式。 */
    XSerialPort_Error m_error;                /**< 最近一次串口错误。 */
    int m_openMode;                           /**< XIODeviceBaseMode 位组合。 */
    uint32_t m_flags;                         /**< XDeviceOpenFlag 位组合。 */
    const void* m_platformData;               /**< 平台配置借用指针。 */
    int64_t m_readBufferSize;                 /**< 接收缓冲上限提示。 */
    int64_t m_pendingWriteBytes;              /**< 已提交但尚未完成的写入字节数。 */
    bool m_dataTerminalReady;                 /**< DTR 已设置状态。 */
    bool m_requestToSend;                     /**< RTS 已设置状态。 */
    bool m_breakEnabled;                      /**< Break 已设置状态。 */
} XDeviceSerialPortContext;

/** @brief XDeviceSerialPort 虚函数表；继承 XDevice，不新增虚槽。 */
XCLASS_DEFINE_BEGING(XDeviceSerialPort)
XCLASS_DEFINE_EXTEND_END(XDeviceSerialPort, XDevice)

/** @brief 串口设备类对象。 */
typedef struct XDeviceSerialPort
{
    XDevice m_base;
} XDeviceSerialPort;

XVtable* XDeviceSerialPort_class_init(void);
void XDeviceSerialPort_init(XDeviceSerialPort* self);
XDeviceSerialPort* XDeviceSerialPort_create(void);
bool XDeviceSerialPort_register(void);

#ifdef __cplusplus
}
#endif
#endif /* XDEVICESERIALPORT_H */
