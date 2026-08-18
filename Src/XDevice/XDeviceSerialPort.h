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
    XSerialPort_Baud1200 = 1200,     /**< 1200 bit/s。 */
    XSerialPort_Baud2400 = 2400,     /**< 2400 bit/s。 */
    XSerialPort_Baud4800 = 4800,     /**< 4800 bit/s。 */
    XSerialPort_Baud9600 = 9600,     /**< 9600 bit/s。 */
    XSerialPort_Baud19200 = 19200,   /**< 19200 bit/s。 */
    XSerialPort_Baud38400 = 38400,   /**< 38400 bit/s。 */
    XSerialPort_Baud57600 = 57600,   /**< 57600 bit/s。 */
    XSerialPort_Baud115200 = 115200  /**< 115200 bit/s。 */
} XSerialPort_BaudRate;

/** @brief 串口数据位。 */
typedef enum XSerialPort_DataBits
{
    XSerialPort_Data5 = 5, /**< 5 个数据位。 */
    XSerialPort_Data6 = 6, /**< 6 个数据位。 */
    XSerialPort_Data7 = 7, /**< 7 个数据位。 */
    XSerialPort_Data8 = 8, /**< 8 个数据位。 */
    XSerialPort_Data9 = 9  /**< 9 个数据位（平台可选）。 */
} XSerialPort_DataBits;

/** @brief 串口校验方式。 */
typedef enum XSerialPort_Parity
{
    XSerialPort_NoParity = 0,    /**< 无校验。 */
    XSerialPort_EvenParity = 2,  /**< 偶校验。 */
    XSerialPort_OddParity = 3,   /**< 奇校验。 */
    XSerialPort_SpaceParity = 4, /**< 空格校验。 */
    XSerialPort_MarkParity = 5   /**< 标记校验。 */
} XSerialPort_Parity;

/** @brief 串口停止位。 */
typedef enum XSerialPort_StopBits
{
    XSerialPort__ZeroPointFive = 0, /**< 0.5 个停止位。 */
    XSerialPort_OneStop = 1,        /**< 1 个停止位。 */
    XSerialPort_TwoStop = 2,        /**< 2 个停止位。 */
    XSerialPort_OneAndHalfStop = 3  /**< 1.5 个停止位。 */
} XSerialPort_StopBits;

/** @brief 串口流控方式。 */
typedef enum XSerialPort_FlowControl
{
    XSerialPort_NoFlowControl = 0, /**< 无流控。 */
    XSerialPort_HardwareControl,   /**< 硬件 RTS/CTS 流控。 */
    XSerialPort_SoftwareControl,   /**< 软件 XON/XOFF 流控。 */
    XSerialPort_BothControl        /**< 同时启用硬件和软件流控。 */
} XSerialPort_FlowControl;

/** @brief 串口引脚信号位组合。 */
typedef enum XSerialPort_PinoutSignal
{
    XSerialPort_NoSignal = 0x00,                    /**< 无有效引脚信号。 */
    XSerialPort_DataTerminalReadySignal = 0x04,    /**< DTR 数据终端就绪。 */
    XSerialPort_DataCarrierDetectSignal = 0x08,    /**< DCD 数据载波检测。 */
    XSerialPort_DataSetReadySignal = 0x10,         /**< DSR 数据设备就绪。 */
    XSerialPort_RingIndicatorSignal = 0x20,        /**< RI 振铃指示。 */
    XSerialPort_RequestToSendSignal = 0x40,        /**< RTS 请求发送。 */
    XSerialPort_ClearToSendSignal = 0x80           /**< CTS 清除发送。 */
} XSerialPort_PinoutSignal;

/** @brief 串口错误类型。 */
typedef enum XSerialPort_Error
{
    XSerialPort_NoError = 0,                 /**< 无错误。 */
    XSerialPort_DeviceNotFoundError,        /**< 找不到串口设备。 */
    XSerialPort_PermissionError,            /**< 权限不足。 */
    XSerialPort_OpenError,                  /**< 打开设备失败。 */
    XSerialPort_WriteError,                 /**< 写入失败。 */
    XSerialPort_ReadError,                  /**< 读取失败。 */
    XSerialPort_ResourceError,              /**< 系统资源不足或设备忙。 */
    XSerialPort_UnsupportedOperationError,  /**< 平台不支持请求操作。 */
    XSerialPort_UnknownError,               /**< 未分类错误。 */
    XSerialPort_TimeoutError,               /**< 操作超时。 */
    XSerialPort_NotOpenError                /**< 串口尚未打开。 */
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
    uint32_t m_dataBits    : 4;              /**< 数据位。 */
    uint32_t m_parity      : 3;              /**< 校验方式。 */
    uint32_t m_stopBits    : 2;              /**< 停止位。 */
    uint32_t m_flowControl : 2;              /**< 流控方式。 */
    uint32_t m_reserved    : 21;             /**< 保留位，必须为 0。 */
    int64_t m_readBufferSize;                /**< 接收缓冲上限提示，单位字节。 */
} XDeviceSerialPortOpenOptions;

/**
 * @brief 初始化串口打开选项为 9600-8-N-1、无流控、512 KiB 接收缓冲。
 * @param options 待初始化选项；不能为 NULL，调用后可覆盖目标端口和其它字段。
 * @note 函数会把整个结构体重置为默认值，调用前已有内容将被覆盖。
 */
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
    const void* m_platformData;               /**< 平台配置借用指针。 */
    int64_t m_readBufferSize;                 /**< 接收缓冲上限提示。 */
    int64_t m_pendingWriteBytes;              /**< 已提交但尚未完成的写入字节数。 */
    int32_t m_baudRate;                       /**< 当前波特率。 */
    uint32_t m_dataBits          : 4;         /**< 当前数据位。 */
    uint32_t m_parity            : 3;         /**< 当前校验方式。 */
    uint32_t m_stopBits          : 2;         /**< 当前停止位。 */
    uint32_t m_flowControl       : 2;         /**< 当前流控方式。 */
    uint32_t m_error             : 4;         /**< 最近一次串口错误。 */
    uint32_t m_dataTerminalReady : 1;         /**< DTR 已设置状态。 */
    uint32_t m_requestToSend     : 1;         /**< RTS 已设置状态。 */
    uint32_t m_breakEnabled      : 1;         /**< Break 已设置状态。 */
    uint32_t m_openMode          : 13;        /**< XIODeviceBaseMode 位组合。 */
    uint32_t m_flags             : 2;         /**< XDeviceOpenFlag 位组合。 */
    uint32_t m_reserved          : 1;         /**< 保留位，必须为 0。 */
} XDeviceSerialPortContext;

/** @brief XDeviceSerialPort 虚函数表；继承 XDevice，不新增虚槽。 */
XCLASS_DEFINE_BEGING(XDeviceSerialPort)
XCLASS_DEFINE_EXTEND_END(XDeviceSerialPort, XDevice)

/** @brief 串口设备类对象。 */
typedef struct XDeviceSerialPort
{
    XDevice m_base;
} XDeviceSerialPort;

/** @brief 初始化串口设备类虚函数表。 @return 共享虚函数表，失败返回 NULL。 */
XVtable* XDeviceSerialPort_class_init(void);
/** @brief 初始化已分配的串口设备对象。 @param self 待初始化对象，不能为 NULL。 */
void XDeviceSerialPort_init(XDeviceSerialPort* self);
/** @brief 创建串口设备类对象。 @return 新对象，失败返回 NULL；调用方负责释放。 */
XDeviceSerialPort* XDeviceSerialPort_create(void);
/** @brief 注册串口设备类别。 @return 首次注册或已注册返回 true，失败返回 false。 */
bool XDeviceSerialPort_register(void);

#ifdef __cplusplus
}
#endif
#endif /* XDEVICESERIALPORT_H */
