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
#include "XDevice_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
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
 *          m_enumerate 为 1 时进入枚举模式：只建立用于查询本机可用串口的设备
 *          上下文，不打开任何具体端口，m_base.m_target 可以为 NULL。
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
    uint32_t m_enumerate   : 1;              /**< 枚举模式：只建立枚举上下文，不打开端口。 */
    uint32_t m_reserved    : 20;             /**< 保留位，必须为 0。 */
    int64_t m_readBufferSize;                /**< 接收缓冲上限提示，单位字节。 */
} XDeviceSerialPortOpenOptions;

/**
 * @brief 初始化串口打开选项为 9600-8-N-1、无流控、512 KiB 接收缓冲。
 * @param options 待初始化选项；不能为 NULL，调用后可覆盖目标端口和其它字段。
 * @note 函数会把整个结构体重置为默认值，调用前已有内容将被覆盖。
 */
void XDeviceSerialPortOpenOptions_init(XDeviceSerialPortOpenOptions* options);

/* ============================================================================
 * 串口枚举：属性值类型、平台采集回调与便捷 API
 * ============================================================================ */

/**
 * @brief 串口条目快照。
 * @details XDeviceSerialPortProperty_AvailablePorts 属性的元素类型。字符串成员
 *          为 UTF-8，由所属列表拥有。
 */
typedef struct XDeviceSerialPortPortInfo
{
    const char* m_portName;      /**< 端口名（如 "COM3"、"/dev/ttyUSB0"）；不可为 NULL。 */
    const char* m_description;   /**< 人类可读描述；平台不支持时为 NULL。 */
    const char* m_manufacturer;  /**< 厂商名；平台不支持时为 NULL。 */
    const char* m_serialNumber;  /**< 序列号；平台不支持时为 NULL。 */
    uint32_t    m_vendorId;      /**< USB 厂商 ID；0 表示未知或非 USB 设备。 */
    uint32_t    m_productId;     /**< USB 产品 ID；0 表示未知或非 USB 设备。 */
    bool        m_busy;          /**< 端口是否已被其它进程占用。 */
} XDeviceSerialPortPortInfo;

/**
 * @brief 串口列表。
 * @details XDeviceSerialPortProperty_AvailablePorts 属性的值类型。
 */
typedef struct XDeviceSerialPortPortList
{
    size_t m_count;                     /**< 条目数。 */
    XDeviceSerialPortPortInfo* m_ports; /**< 条目数组；与列表同生命周期。 */
} XDeviceSerialPortPortList;

/**
 * @brief 平台串口采集回调。
 * @param port 当前条目；借用，回调返回后失效。
 * @param userData 平台后端原样传入的借用数据；可为 NULL。
 * @return 返回 false 表示提前终止枚举。
 * @note 该回调只供平台后端实现 XDeviceSerialPort 的私有枚举钩子时使用；
 *       业务代码不直接调用。
 */
typedef bool (*XDeviceSerialPortPortCollector)(void* userData, const XDeviceSerialPortPortInfo* port);

/**
 * @brief 释放串口列表及其条目字符串。
 * @param list 待释放的列表；可为 NULL。
 * @note 只用于释放 XDeviceSerialPort_availablePorts 返回的自有列表；
 *       作为属性值借用的列表由设备上下文释放，不得用本函数释放。
 */
void XDeviceSerialPortPortList_delete(XDeviceSerialPortPortList* list);

/* ============================================================================
 * XDeviceSerialPort 便捷 API（统一通过 XFd 门面）
 * ============================================================================ */

/** @brief 复用 XDevice 的打开、关闭与控制门面；参数契约与父类 API 相同。 */
#define XDeviceSerialPort_open(options, error) \
    XDevice_openClass("serial", (const XDeviceOpenOptions*)(options), (error))
#define XDeviceSerialPort_close   XDevice_close
#define XDeviceSerialPort_control XDevice_control
#define XDeviceSerialPort_read    XDevice_read
#define XDeviceSerialPort_write   XDevice_write
#define XDeviceSerialPort_getProperty    XDevice_getProperty
#define XDeviceSerialPort_setProperty    XDevice_setProperty
#define XDeviceSerialPort_queryProperty  XDevice_queryProperty
/**
 * @brief 刷新串口缓冲；等待平台把已提交的写数据全部发完。
 * @param fd 已打开的串口描述符；借用。
 * @return 平台确认发送完成返回 true；失败返回 false。
 */
#define XDeviceSerialPort_flush XDevice_flush
/** @brief 读取设备最近一次错误码；复用父类门面。 */
#define XDeviceSerialPort_lastError XDevice_lastError

/**
 * @brief 列出本机当前可用的串口。
 * @details 便捷 API：内部读取 XDeviceSerialPortProperty_AvailablePorts 属性，
 *          返回一份由调用方拥有的列表副本。
 * @param fd 已打开的串口设备描述符；须以枚举模式打开（打开选项 m_enumerate=1）。
 * @return 新分配的列表，需用 XDeviceSerialPortPortList_delete 释放；失败返回 NULL。
 */
XDeviceSerialPortPortList* XDeviceSerialPort_availablePorts(XFd fd);

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
    XDeviceSerialPortProperty_AvailablePorts
        /**< 只读，值为借用 XDeviceSerialPortPortList*；列出本机可用串口。列表由设备上下文持有，设备关闭后失效。 */,
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
    XDeviceSerialPortPortList m_availablePorts; /**< AvailablePorts 属性的借用缓存；由上下文拥有。 */
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
    uint32_t m_enumerate         : 1;         /**< 枚举模式：只建立枚举上下文，未打开具体端口。 */
    void (*m_readableNotify)(void* ctx);      /**< 可读事件回调（平台层注册，见下）。 */
    void* m_readableCtx;                      /**< 可读事件回调的属主上下文。 */
} XDeviceSerialPortContext;

/**
 * @brief 串口「可读事件源」平台钩子（跨平台统一的等待集接入）。
 * @details 串口接收不再靠上层毫秒级定时器轮询（XSerialPort 的 2ms RX 轮询
 *          拍），而是把串口 fd/HANDLE 注册进**框架自己的异步后端等待集**：
 *          POSIX 侧走 XAbstractNetIoRing（epoll / io_uring 双引擎，语义一致：
 *          「fd 就绪 ⇒ 唤醒阻塞中的事件循环 ⇒ 回调属主」）；
 *          Windows 侧走框架 IOCP（OVERLAPPED 读完成）。
 *          数据到达即唤醒主循环，不再等「下一圈边界」——这是把 485 轮询
 *          从「等下一拍」变成「事件驱动」的关键。
 *
 * @param fd     串口设备上下文（XDevice 层的 fd 标识）。
 * @param notify 可读回调（属主在回调里把数据取走并发射 readyRead）。
 * @param ctx    回调属主上下文。
 * @return true=已注册（此后由事件驱动）；false=该平台/配置不支持
 *         （调用方须保留定时器轮询兜底，行为退化为改造前口径）。
 */
bool XDeviceSerialPort_platformArmReadable(XFd fd,
                                           void (*notify)(void* ctx),
                                           void* ctx);
/**
 * @brief 注销串口「可读事件源」（关闭串口前必须调用，避免等待集里留着
 *        一个即将失效的 fd ⇒ 事件循环在其他 fd 就绪时读到已关闭 fd）。
 */
void XDeviceSerialPort_platformDisarmReadable(XFd fd);

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
