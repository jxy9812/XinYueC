#ifndef XCANBUSDEVICE_H
#define XCANBUSDEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "XObject.h"
#include "XMutex.h"
#include "XCanBusFrame.h"
#include "XCanBusDeviceInfo.h"
#include "XString.h"
#include "XVector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XCanBusDevice.h
 * @brief CAN 总线设备抽象基类头文件（对齐 Qt6 QCanBusDevice）
 * @details 实现 CAN 总线设备的公共接口，包括连接管理、配置参数、
 *          帧收发、状态/错误管理等。这是一个抽象基类，必须由子类
 *          （如 SocketCanBackend、VirtualCanBackend）实现 open/close
 *          和 writeFrame 等纯虚函数。
 *
 * @par 功能特性
 * - 设备状态管理（未连接、连接中、已连接、关闭中）
 * - 错误处理机制（多种错误类型）
 * - 配置参数管理（过滤器、位速率、CAN FD 等）
 * - 帧收发队列（接收帧入队、发送帧出队）
 * - 信号机制（帧接收、帧写入、状态改变、错误发生）
 * - 总线状态查询（Good/Warning/Error/BusOff）
 * - 控制器复位
 *
 * @par 类层次结构
 * @code
 * XObject (基类)
 *   └── XCanBusDevice (CAN 设备抽象基类)
 *         ├── SocketCanBackend (SocketCAN 后端)
 *         ├── VirtualCanBackend (虚拟 CAN 后端)
 *         ├── PeakCanBackend (PEAK 硬件后端)
 *         ├── VectorCanBackend (Vector 硬件后端)
 *         ├── SystecCanBackend (Systec 硬件后端)
 *         ├── TinyCanBackend (TinyCAN 硬件后端)
 *         └── PassThruCanBackend (J2534 PassThru 后端)
 * @endcode
 *
 * @par 使用示例
 * @code
 * // 子类使用示例
 * XCanBusDevice* device = (XCanBusDevice*)socketCanBackend;
 *
 * // 设置配置参数
 * XCanBusDevice_setConfigurationParameter(device,
 *     XCanBusDevice_RawFilterKey, XVariant_create_list(...));
 *
 * // 连接设备
 * if (XCanBusDevice_connectDevice(device)) {
 *     // 发送帧
 *     XCanBusFrame frame;
 *     XCanBusFrame_init(&frame, XCanBusFrame_DataFrame);
 *     XCanBusFrame_setFrameId(&frame, 0x123);
 *     XCanBusDevice_writeFrame(device, &frame);
 *     XCanBusFrame_deinit(&frame);
 * }
 *
 * // 断开连接
 * XCanBusDevice_disconnectDevice(device);
 *
 * // 清理
 * XCanBusDevice_deleteLater(device);
 * @endcode
 *
 * @note 此为抽象基类，必须由子类实现 open()、close()、writeFrame() 和 interpretErrorFrame()
 */

/******************************************************************************************
 * 虚函数表枚举定义
 ******************************************************************************************/

XCLASS_DEFINE_BEGING(XCanBusDevice)
XCLASS_DEFINE_ENUM(XCanBusDevice, Open) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XCanBusDevice, Close),
XCLASS_DEFINE_ENUM(XCanBusDevice, WriteFrame),
XCLASS_DEFINE_ENUM(XCanBusDevice, InterpretErrorFrame),
XCLASS_DEFINE_ENUM(XCanBusDevice, ResetController),
XCLASS_DEFINE_ENUM(XCanBusDevice, HasBusStatus),
XCLASS_DEFINE_ENUM(XCanBusDevice, BusStatus),
XCLASS_DEFINE_ENUM(XCanBusDevice, DeviceInfo),
XCLASS_DEFINE_END(XCanBusDevice)

/******************************************************************************************
 * 枚举类型定义
 ******************************************************************************************/

/**
 * @brief CAN 总线设备错误类型枚举
 * @details 对齐 Qt6 QCanBusDevice::CanBusError
 */
typedef enum {
    XCanBusDevice_NoError = 0,              ///< 无错误
    XCanBusDevice_ReadError,                ///< 读取错误
    XCanBusDevice_WriteError,               ///< 写入错误
    XCanBusDevice_ConnectionError,          ///< 连接错误
    XCanBusDevice_ConfigurationError,       ///< 配置错误
    XCanBusDevice_UnknownError,             ///< 未知错误
    XCanBusDevice_OperationError,           ///< 操作错误（设备状态不允许操作）
    XCanBusDevice_TimeoutError              ///< 超时错误
} XCanBusDevice_Error;

/**
 * @brief CAN 总线设备连接状态枚举
 * @details 对齐 Qt6 QCanBusDevice::CanBusDeviceState
 */
typedef enum {
    XCanBusDevice_UnconnectedState = 0,     ///< 未连接状态
    XCanBusDevice_ConnectingState,          ///< 连接中状态
    XCanBusDevice_ConnectedState,           ///< 已连接状态
    XCanBusDevice_ClosingState              ///< 关闭中状态
} XCanBusDevice_State;

/**
 * @brief CAN 总线状态枚举
 * @details 对齐 Qt6 QCanBusDevice::CanBusStatus
 */
typedef enum {
    XCanBusDevice_CanBusStatus_Unknown,     ///< 未知状态
    XCanBusDevice_CanBusStatus_Good,        ///< 状态良好
    XCanBusDevice_CanBusStatus_Warning,     ///< 警告状态
    XCanBusDevice_CanBusStatus_Error,       ///< 错误状态
    XCanBusDevice_CanBusStatus_BusOff       ///< 总线关闭
} XCanBusDevice_CanBusStatus;

/**
 * @brief CAN 设备配置键枚举
 * @details 对齐 Qt6 QCanBusDevice::ConfigurationKey
 */
typedef enum {
    XCanBusDevice_RawFilterKey = 0,         ///< 原始过滤器（QList<XCanBusDevice_Filter>）
    XCanBusDevice_ErrorFilterKey,           ///< 错误过滤器（XCanBusFrame_FrameErrors）
    XCanBusDevice_LoopbackKey,              ///< 回环模式（bool）
    XCanBusDevice_ReceiveOwnKey,            ///< 接收自身发送（bool）
    XCanBusDevice_BitRateKey,               ///< CAN 位速率（bps）
    XCanBusDevice_CanFdKey,                 ///< CAN FD 启用（bool）
    XCanBusDevice_DataBitRateKey,           ///< CAN FD 数据位速率（bps）
    XCanBusDevice_ProtocolKey,              ///< 协议选择
    XCanBusDevice_UserKey = 30              ///< 用户自定义键起始值
} XCanBusDevice_ConfigurationKey;

/**
 * @brief CAN 帧过滤器格式枚举
 * @details 对齐 Qt6 QCanBusDevice::Filter::FormatFilter
 */
typedef enum {
    XCanBusDevice_MatchBaseFormat = 0x0001,             ///< 匹配标准帧格式（11 位 ID）
    XCanBusDevice_MatchExtendedFormat = 0x0002,         ///< 匹配扩展帧格式（29 位 ID）
    XCanBusDevice_MatchBaseAndExtendedFormat = 0x0003   ///< 匹配所有帧格式
} XCanBusDevice_FormatFilter;

/**
 * @brief CAN 帧过滤器结构体
 * @details 对齐 Qt6 QCanBusDevice::Filter，用于配置设备接收哪些帧
 */
typedef struct XCanBusDevice_Filter {
    uint32_t m_frameId;                     ///< 帧 ID 匹配值
    uint32_t m_frameIdMask;                 ///< 帧 ID 掩码
    XCanBusFrame_FrameType m_type;          ///< 帧类型匹配
    XCanBusDevice_FormatFilter m_format;    ///< 帧格式匹配
} XCanBusDevice_Filter;

/**
 * @brief 比较两个 Filter 是否相等（对齐 Qt6 operator==）
 * @param a 过滤器 A
 * @param b 过滤器 B
 * @return 相等返回 true
 */
static inline bool XCanBusDevice_Filter_equals(const XCanBusDevice_Filter* a, const XCanBusDevice_Filter* b)
{
    if (!a || !b) return false;
    return a->m_frameId == b->m_frameId && a->m_frameIdMask == b->m_frameIdMask
        && a->m_type == b->m_type && a->m_format == b->m_format;
}

/**
 * @brief 比较两个 Filter 是否不相等（对齐 Qt6 operator!=）
 * @param a 过滤器 A
 * @param b 过滤器 B
 * @return 不相等返回 true
 */
static inline bool XCanBusDevice_Filter_notEquals(const XCanBusDevice_Filter* a, const XCanBusDevice_Filter* b)
{
    return !XCanBusDevice_Filter_equals(a, b);
}

/**
 * @brief 清除方向枚举
 * @details 对齐 Qt6 QCanBusDevice::Direction
 */
typedef enum {
    XCanBusDevice_Input = 1,                ///< 输入方向（接收队列）
    XCanBusDevice_Output = 2,               ///< 输出方向（发送队列）
    XCanBusDevice_AllDirections = 3         ///< 所有方向
} XCanBusDevice_Direction;

/******************************************************************************************
 * 配置条目结构体
 ******************************************************************************************/

/**
 * @brief 配置参数条目
 * @details 存储配置键和对应的值
 */
typedef struct XCanBusDevice_ConfigEntry {
    XCanBusDevice_ConfigurationKey m_key;   ///< 配置键
    void* m_value;                          ///< 配置值（XVariant*）
} XCanBusDevice_ConfigEntry;

/******************************************************************************************
 * XCanBusDevice 结构体
 ******************************************************************************************/

/**
 * @brief CAN 总线设备结构体
 * @details 继承自 XObject，封装 CAN 总线设备的公共属性和状态。
 *          包含连接状态、错误信息、帧队列、配置参数等。
 *          子类需重写 open/close/writeFrame/interpretErrorFrame 等虚函数。
 */
typedef struct XCanBusDevice {
    XObject m_class;                        ///< 继承自 XObject 基类

    XCanBusDevice_State m_state;            ///< 设备连接状态
    XCanBusDevice_Error m_error;            ///< 当前错误码
    XString* m_errorString;                 ///< 错误描述字符串

    XVector* m_incomingFrames;              ///< 接收帧队列（XVector<XCanBusFrame*>）
    XVector* m_outgoingFrames;              ///< 发送帧队列（XVector<XCanBusFrame*>）

    XVector* m_configOptions;               ///< 配置参数列表（XVector<XCanBusDevice_ConfigEntry>）

    bool m_waitForReceivedEntered;          ///< 是否已进入等待接收状态
    bool m_waitForWrittenEntered;           ///< 是否已进入等待写入状态
    XMutex* m_incomingFramesGuard;          /*< 接收帧队列互斥锁 */
} XCanBusDevice;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

/**
 * @brief 初始化 XCanBusDevice 的虚函数表
 * @return 指向初始化完成的 XVtable 的指针
 * @note 该函数是线程安全的，多次调用返回同一虚表实例
 */
XVtable* XCanBusDevice_class_init(void);

/**
 * @brief 在堆上创建并初始化一个 XCanBusDevice 实例
 * @return 成功返回指向新分配 XCanBusDevice 对象的指针，失败返回 NULL
 * @note 返回的对象必须通过 XObject_deleteLater 或 XCanBusDevice_delete_base 释放
 */
XCanBusDevice* XCanBusDevice_create(void);

/**
 * @brief 初始化一个已分配的 XCanBusDevice 实例
 * @param dev 待初始化的 XCanBusDevice 对象指针（非 NULL）
 */
void XCanBusDevice_init(XCanBusDevice* dev);

/******************************************************************************************
 * 配置参数 API（对齐 QCanBusDevice）
 ******************************************************************************************/

/**
 * @brief 设置配置参数
 * @param dev 设备指针（非 NULL）
 * @param key 配置键
 * @param value 配置值（XVariant*，深拷贝）
 * @note 子类可重写此方法以处理特定配置
 */
void XCanBusDevice_setConfigurationParameter(XCanBusDevice* dev,
    XCanBusDevice_ConfigurationKey key, void* value);

/**
 * @brief 获取配置参数
 * @param dev 设备指针（非 NULL）
 * @param key 配置键
 * @return 配置值的深拷贝（XVariant*），调用者负责释放；未设置返回 NULL
 */
void* XCanBusDevice_configurationParameter(const XCanBusDevice* dev,
    XCanBusDevice_ConfigurationKey key);

/**
 * @brief 获取所有已配置的键列表
 * @param dev 设备指针（非 NULL）
 * @return 配置键列表（XVector<int>*），调用者负责释放
 */
XVector* XCanBusDevice_configurationKeys(const XCanBusDevice* dev);

/******************************************************************************************
 * 帧收发 API（对齐 QCanBusDevice）
 ******************************************************************************************/

/**
 * @brief 写入 CAN 帧（纯虚函数，子类必须重写）
 * @param dev 设备指针（非 NULL）
 * @param frame 待写入的 CAN 帧指针
 * @return 写入成功返回 true，失败返回 false
 * @note 子类必须实现此函数。对于 RTR 帧，负载长度表示期望的响应长度
 */
bool XCanBusDevice_writeFrame(XCanBusDevice* dev, const XCanBusFrame* frame);

/**
 * @brief 读取一个 CAN 帧
 * @param dev 设备指针（非 NULL）
 * @return 读取的 CAN 帧（堆分配），调用者负责释放；无帧返回 NULL
 * @note 从内部接收队列取出最早的一个帧
 */
XCanBusFrame* XCanBusDevice_readFrame(XCanBusDevice* dev);

/**
 * @brief 读取所有可用的 CAN 帧
 * @param dev 设备指针（非 NULL）
 * @return 帧列表（XVector<XCanBusFrame*>*），调用者负责释放
 * @note 从内部接收队列取出所有帧
 */
XVector* XCanBusDevice_readAllFrames(XCanBusDevice* dev);

/**
 * @brief 获取接收队列中可用帧数
 * @param dev 设备指针（非 NULL）
 * @return 可用帧数
 */
int64_t XCanBusDevice_framesAvailable(const XCanBusDevice* dev);

/**
 * @brief 获取发送队列中待发送帧数
 * @param dev 设备指针（非 NULL）
 * @return 待发送帧数
 */
int64_t XCanBusDevice_framesToWrite(const XCanBusDevice* dev);

/******************************************************************************************
 * 控制器管理 API（对齐 QCanBusDevice）
 ******************************************************************************************/

/**
 * @brief 复位控制器（虚函数，子类可重写）
 * @param dev 设备指针（非 NULL）
 * @note 默认实现为空操作
 */
void XCanBusDevice_resetController(XCanBusDevice* dev);

/**
 * @brief 检查设备是否支持总线状态查询（虚函数，子类可重写）
 * @param dev 设备指针（非 NULL）
 * @return 支持返回 true，否则返回 false
 * @note 默认返回 false
 */
bool XCanBusDevice_hasBusStatus(const XCanBusDevice* dev);

/**
 * @brief 获取总线状态（虚函数，子类可重写）
 * @param dev 设备指针（非 NULL）
 * @return 总线状态枚举值
 * @note 默认返回 XCanBusDevice_CanBusStatus_Unknown
 */
XCanBusDevice_CanBusStatus XCanBusDevice_busStatus(XCanBusDevice* dev);

/**
 * @brief 清除帧队列
 * @param dev 设备指针（非 NULL）
 * @param direction 清除方向（Input/Output/AllDirections）
 */
void XCanBusDevice_clear(XCanBusDevice* dev, XCanBusDevice_Direction direction);

/******************************************************************************************
 * 等待 API（对齐 QCanBusDevice）
 ******************************************************************************************/

/**
 * @brief 等待帧写入完成
 * @param dev 设备指针（非 NULL）
 * @param msecs 等待超时时间（毫秒）
 * @return 写入完成返回 true，超时返回 false
 * @note 虚函数，子类可重写。默认实现等待 framesToWrite() 变为 0
 */
bool XCanBusDevice_waitForFramesWritten(XCanBusDevice* dev, int msecs);

/**
 * @brief 等待帧接收
 * @param dev 设备指针（非 NULL）
 * @param msecs 等待超时时间（毫秒）
 * @return 接收到帧返回 true，超时返回 false
 * @note 虚函数，子类可重写。默认实现等待 framesAvailable() > 0
 */
bool XCanBusDevice_waitForFramesReceived(XCanBusDevice* dev, int msecs);

/******************************************************************************************
 * 连接管理 API（对齐 QCanBusDevice）
 ******************************************************************************************/

/**
 * @brief 连接设备到 CAN 总线
 * @param dev 设备指针（非 NULL）
 * @return 连接成功返回 true，失败返回 false
 * @note 内部调用 open() 虚函数。如果设备已连接，返回 false 并设置 ConnectionError
 */
bool XCanBusDevice_connectDevice(XCanBusDevice* dev);

/**
 * @brief 断开设备与 CAN 总线的连接
 * @param dev 设备指针（非 NULL）
 * @note 内部调用 close() 虚函数。如果设备未连接，输出警告
 */
void XCanBusDevice_disconnectDevice(XCanBusDevice* dev);

/******************************************************************************************
 * 状态/错误查询 API（对齐 QCanBusDevice）
 ******************************************************************************************/

/**
 * @brief 获取设备当前状态
 * @param dev 设备指针（可为 NULL）
 * @return 设备状态，dev 为 NULL 时返回 XCanBusDevice_UnconnectedState
 */
XCanBusDevice_State XCanBusDevice_state(const XCanBusDevice* dev);

/**
 * @brief 获取设备当前错误码
 * @param dev 设备指针（可为 NULL）
 * @return 错误码，dev 为 NULL 时返回 XCanBusDevice_UnknownError
 */
XCanBusDevice_Error XCanBusDevice_error(const XCanBusDevice* dev);

/**
 * @brief 获取设备错误描述字符串
 * @param dev 设备指针（可为 NULL）
 * @return 错误描述字符串的深拷贝，调用者负责释放
 * @note 如果没有设置错误字符串，返回默认的错误描述
 */
XString* XCanBusDevice_errorString(const XCanBusDevice* dev);

/**
 * @brief 解释错误帧（纯虚函数，子类必须重写）
 * @param dev 设备指针（非 NULL）
 * @param errorFrame 错误帧
 * @return 错误描述字符串，调用者负责释放
 */
XString* XCanBusDevice_interpretErrorFrame(XCanBusDevice* dev, const XCanBusFrame* errorFrame);

/**
 * @brief 获取设备信息（虚函数，子类可重写）
 * @param dev 设备指针（非 NULL）
 * @param info 输出参数，设备信息结构体（需已初始化）
 * @return 成功返回 true，失败返回 false
 * @note 默认返回默认构造的设备信息
 */
bool XCanBusDevice_deviceInfo(XCanBusDevice* dev, XCanBusDeviceInfo* info);

/******************************************************************************************
 * 信号接口（对齐 QCanBusDevice）
 ******************************************************************************************/

/**
 * @brief 发射错误发生信号
 * @param dev 设备指针（非 NULL）
 * @param error 错误码
 */
void* XCanBusDevice_errorOccurred_signal(XCanBusDevice* dev, XCanBusDevice_Error error);

/**
 * @brief 发射帧接收信号
 * @param dev 设备指针（非 NULL）
 */
void* XCanBusDevice_framesReceived_signal(XCanBusDevice* dev);

/**
 * @brief 发射帧写入信号
 * @param dev 设备指针（非 NULL）
 * @param framesCount 写入的帧数
 */
void* XCanBusDevice_framesWritten_signal(XCanBusDevice* dev, int64_t framesCount);

/**
 * @brief 发射状态改变信号
 * @param dev 设备指针（非 NULL）
 * @param state 新状态
 */
void* XCanBusDevice_stateChanged_signal(XCanBusDevice* dev, XCanBusDevice_State state);

/******************************************************************************************
 * 内存管理宏
 ******************************************************************************************/

#define XCanBusDevice_deleteLater   XObject_deleteLater
#define XCanBusDevice_deinitLater   XObject_deinitLater

#ifdef __cplusplus
}
#endif

#endif // XCANBUSDEVICE_H
