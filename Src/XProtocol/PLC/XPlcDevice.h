#ifndef XPLCDEVICE_H
#define XPLCDEVICE_H

#include "XPlc_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "XObject.h"
#include "XVariant.h"
#include "XString.h"
#include "XMap.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XPLC_CORE_ON

/**
 * @file XPlcDevice.h
 * @brief PLC 设备基类（对齐 Qt6 QModbusDevice，扩充 S7 连接参数与握手状态）
 * @details 实现 PLC 设备的公共接口：连接管理、参数配置、状态/错误管理与信号机制。
 *          与 XModbusDevice 相比：状态机多一档 Handshaking（S7 有应用层握手，
 *          TCP connected 不等于可收发）；连接参数增加 Rack/Slot/TSAP。
 *
 * @par 功能特性
 * - 设备状态管理（未连接、连接中、握手中、已连接、关闭中）
 * - 错误处理机制（错误码与 XModbusDevice 逐项同名同序，上层处理逻辑可复用）
 * - 连接参数配置（IP/端口/Rack/Slot/本端与目标 TSAP）
 * - 信号机制（stateChanged / errorOccurred）
 *
 * @par 使用示例
 * @code
 * XS7TcpClient* client = XS7TcpClient_create();
 * XPlcDevice_setConnectionParameter_ref((XPlcDevice*)client,
 *     XPlcDevice_NetworkAddressParameter, XVariant_create_utf8_str("192.168.0.1"));
 * XPlcDevice_setConnectionParameter_ref((XPlcDevice*)client,
 *     XPlcDevice_NetworkPortParameter, XVariant_create_int(102));
 * XPlcDevice_setConnectionParameter_ref((XPlcDevice*)client,
 *     XPlcDevice_RackParameter, XVariant_create_int(0));
 * XPlcDevice_setConnectionParameter_ref((XPlcDevice*)client,
 *     XPlcDevice_SlotParameter, XVariant_create_int(2));
 * if (XPlcDevice_connectDevice((XPlcDevice*)client)) {
 *     // 连接发起成功（异步），需等待 stateChanged 到 ConnectedState
 * }
 * XPlcDevice_disconnectDevice((XPlcDevice*)client);
 * XObject_deleteLater(client);
 * @endcode
 *
 * @note 此为抽象基类，必须由子类（如 XS7TcpClient）实现 open/close 虚槽
 */

 /******************************************************************************************
  * 虚函数表枚举定义
  ******************************************************************************************/

XCLASS_DEFINE_BEGING(XPlcDevice)
XCLASS_DEFINE_ENUM(XPlcDevice, Open) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XPlcDevice, Close),
XCLASS_DEFINE_END(XPlcDevice)

/******************************************************************************************
 * 枚举类型定义
 ******************************************************************************************/

 /**
  * @brief PLC 设备错误类型枚举
  * @details 与 XModbusDevice_Error 逐项同名同序，便于上层统一处理。
  *          S7 应答细节错误（error class/code、数据区 0x05/0x06/0x0A）不扩枚举，
  *          写入 errorString 并置 ProtocolError。
  */
typedef enum {
    XPlcDevice_NoError = 0,              ///< 无错误
    XPlcDevice_ReadError,                ///< 读取错误
    XPlcDevice_WriteError,               ///< 写入错误
    XPlcDevice_ConnectionError,          ///< 连接错误
    XPlcDevice_ConfigurationError,       ///< 配置错误
    XPlcDevice_TimeoutError,             ///< 超时错误
    XPlcDevice_ProtocolError,            ///< 协议错误
    XPlcDevice_ReplyAbortedError,        ///< 响应被中止
    XPlcDevice_UnknownError,             ///< 未知错误
    XPlcDevice_InvalidResponseError      ///< 无效响应
} XPlcDevice_Error;

/**
 * @brief PLC 设备连接状态枚举
 * @details 对齐 Qt6 QModbusDevice::State 并新增 Handshaking 中间态：
 *          S7 完成 TCP 连接后还需 COTP CR/CC 与 SetupCommunication 握手，
 *          握手完成前不可收发业务报文。
 */
typedef enum {
    XPlcDevice_UnconnectedState = 0,     ///< 未连接状态
    XPlcDevice_ConnectingState,          ///< 连接中状态（TCP connect 已发起）
    XPlcDevice_HandshakingState,         ///< 握手中状态（COTP/SetupCommunication 进行中，S7 特有）
    XPlcDevice_ConnectedState,           ///< 已连接状态（握手完成、PDU 长度已协商，可收发业务）
    XPlcDevice_ClosingState              ///< 关闭中状态
} XPlcDevice_State;

/**
 * @brief PLC 设备连接参数枚举
 * @details 在 Modbus 网络参数基础上扩充 Rack/Slot/TSAP（西门子 ISO-on-TCP 必需）。
 * @par 参数说明
 * | 参数 | 类型 | 说明 |
 * |------|------|------|
 * | NetworkAddressParameter | string | IP 地址，如 "192.168.0.1" |
 * | NetworkPortParameter | int | 端口号，S7 默认 102 |
 * | RackParameter | int | 机架号 0..7（编入目标 TSAP 高 3 位） |
 * | SlotParameter | int | 槽号 0..31（编入目标 TSAP 低 5 位） |
 * | LocalTsapParameter | int | 本端 TSAP，默认 0x0100（Logo/S200 类可改） |
 * | RemoteTsapParameter | int | -1=按 Rack/Slot 自动；≥0 直接用作目标 TSAP |
 */
typedef enum {
    XPlcDevice_NetworkAddressParameter = 0,  ///< 网络地址参数（string：IP）
    XPlcDevice_NetworkPortParameter,         ///< 网络端口参数（int：默认 102）
    XPlcDevice_RackParameter,                ///< 机架号参数（int：0..7）
    XPlcDevice_SlotParameter,                ///< 槽号参数（int：0..31）
    XPlcDevice_LocalTsapParameter,           ///< 本端 TSAP 参数（int：默认 0x0100）
    XPlcDevice_RemoteTsapParameter,          ///< 目标 TSAP 参数（int：-1=按 Rack/Slot 自动）
    XPlcDevice_ParameterCount                ///< 参数总数
} XPlcDevice_ConnectionParameter;

/**
 * @brief PLC 设备中间错误类型枚举
 * @details 用于 Reply 分片/批量过程中的部分失败记录，
 *          与 XModbusDevice_IntermediateError 对位（CRC 一项由分片错误替代）。
 */
typedef enum {
    XPlcDevice_ChunkError = 0,              ///< 分片部分失败（读写多片中某片超时/出错）
    XPlcDevice_ResponseRequestMismatch      ///< 响应与请求不匹配（pduRef 错配等）
} XPlcDevice_IntermediateError;

/******************************************************************************************
 * 结构体定义
 ******************************************************************************************/

 /**
  * @brief PLC 设备基类结构体
  * @details 继承自XObject，提供PLC设备的公共接口
  *
  * @par 成员说明
  * | 成员 | 类型 | 说明 |
  * |------|------|------|
  * | m_class | XObject | 基类，继承自XObject，第一位，禁手工修改 |
  * | m_state | uint16_t | 当前设备连接状态（XPlcDevice_State） |
  * | m_error | uint16_t | 当前错误码（XPlcDevice_Error） |
  * | m_errorString | XString* | 错误描述字符串 |
  * | m_params | XVariant*[] | 连接参数数组（深拷贝持有，deinit 逐个释放） |
  * | m_ioDevice | XIODevice* | 底层IO设备（由子类创建/销毁） |
  */
typedef struct XPlcDevice {
    XObject m_class;                                      ///< 继承自XObject基类
    uint16_t/*XPlcDevice_State*/ m_state;                 ///< 当前设备连接状态
    uint16_t/*XPlcDevice_Error*/ m_error;                 ///< 当前错误码
    XString* m_errorString;                               ///< 错误描述字符串
    XVariant* m_params[XPlcDevice_ParameterCount];        ///< 连接参数数组
    XIODevice* m_ioDevice;                                ///< 底层IO设备
} XPlcDevice;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

 /**
  * @brief 初始化XPlcDevice的虚函数表
  * @return 初始化完成的虚函数表指针
  * @note 该函数是线程安全的，多次调用返回同一虚表实例
  */
XVtable* XPlcDevice_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XPlcDevice实例
 * @param memory 内存分配类型
 * @return 成功返回指向新分配XPlcDevice对象的指针，失败返回NULL
 * @note 返回的对象必须通过 XObject_deleteLater 释放
 * @warning 此为抽象基类，通常不应直接创建实例，而应使用子类的创建函数
 */
XPlcDevice* XPlcDevice_create_ex(XMemoryType memory);

/**
 * @brief 初始化一个已分配的XPlcDevice实例
 * @param dev 待初始化的XPlcDevice对象指针（非NULL）
 * @note 初始化基类成员、设置默认状态和错误值
 * @par 默认值
 * - 状态：XPlcDevice_UnconnectedState
 * - 错误：XPlcDevice_NoError
 * - 参数：全部为NULL
 * - IO设备：NULL
 */
void XPlcDevice_init(XPlcDevice* dev);

/******************************************************************************************
 * 连接参数接口
 ******************************************************************************************/

 /**
  * @brief 获取连接参数值（深拷贝）
  * @param dev XPlcDevice实例指针
  * @param parameter 参数类型（XPlcDevice_ConnectionParameter枚举）
  * @return 成功返回参数值的XVariant副本（调用者负责释放），失败返回NULL
  * @par 使用示例
  * @code
  * XVariant* port = XPlcDevice_connectionParameter(device, XPlcDevice_NetworkPortParameter);
  * if (port) {
  *     int portNum = XVariant_toInt(port);
  *     XClassDelete(port);
  * }
  * @endcode
  */
XVariant* XPlcDevice_connectionParameter(const XPlcDevice* dev, XPlcDevice_ConnectionParameter parameter);

/**
 * @brief 获取连接参数值（常量引用，不复制）
 * @param dev XPlcDevice实例指针
 * @param parameter 参数类型
 * @return 参数值的常量指针（借用，调用者不应释放），不存在返回NULL
 */
const XVariant* XPlcDevice_connectionParameter_const(const XPlcDevice* dev, XPlcDevice_ConnectionParameter parameter);

/**
 * @brief 设置连接参数（深拷贝）
 * @param dev 设备指针（非NULL）
 * @param parameter 参数类型
 * @param value 参数值（内部复制，调用者可安全释放原value；NULL忽略）
 */
void XPlcDevice_setConnectionParameter(XPlcDevice* dev, XPlcDevice_ConnectionParameter parameter, XVariant* value);

/**
 * @brief 设置连接参数（移动语义）
 * @param dev 设备指针（非NULL）
 * @param parameter 参数类型
 * @param value 参数值（移动，函数内接管所有权）
 */
void XPlcDevice_setConnectionParameter_move(XPlcDevice* dev, XPlcDevice_ConnectionParameter parameter, XVariant* value);

/**
 * @brief 设置连接参数（引用语义）
 * @param dev 设备指针（非NULL）
 * @param parameter 参数类型
 * @param value 参数值（引用，函数内不释放；调用者不得在设备释放前自行释放）
 */
void XPlcDevice_setConnectionParameter_ref(XPlcDevice* dev, XPlcDevice_ConnectionParameter parameter, XVariant* value);

/******************************************************************************************
 * 连接管理接口
 ******************************************************************************************/

 /**
  * @brief 连接设备（异步）
  * @param dev 设备指针（非NULL）
  * @return 连接发起成功返回true，失败返回false
  * @note 内部经虚函数表调用子类 open()；返回 true 仅表示发起成功，
  *       须等待 stateChanged 信号到 XPlcDevice_ConnectedState 才可收发业务
  *       （S7 需先完成 COTP/SetupCommunication 握手）。
  */
bool XPlcDevice_connectDevice(XPlcDevice* dev);

/**
 * @brief 断开设备连接
 * @param dev 设备指针（非NULL）
 * @note 内部经虚函数表调用子类 close()，状态回到 XPlcDevice_UnconnectedState
 */
void XPlcDevice_disconnectDevice(XPlcDevice* dev);

/******************************************************************************************
 * 状态/错误查询接口
 ******************************************************************************************/

 /**
  * @brief 获取设备当前状态
  * @param dev XPlcDevice实例指针
  * @return 设备状态，dev为NULL时返回XPlcDevice_UnconnectedState
  */
XPlcDevice_State XPlcDevice_state(const XPlcDevice* dev);

/**
 * @brief 获取设备当前错误码
 * @param dev XPlcDevice实例指针
 * @return 错误码，dev为NULL时返回XPlcDevice_UnknownError
 */
XPlcDevice_Error XPlcDevice_error(const XPlcDevice* dev);

/**
 * @brief 获取设备错误描述字符串（深拷贝）
 * @param dev XPlcDevice实例指针
 * @return 错误描述字符串的副本，调用者负责释放（XClassDelete）
 * @note 无自定义错误串时返回默认错误描述
 */
XString* XPlcDevice_errorString(const XPlcDevice* dev);

/**
 * @brief 获取底层IO设备
 * @param dev XPlcDevice实例指针
 * @return 底层IO设备指针，dev为NULL或无IO设备时返回NULL
 * @note 返回的IO设备由子类管理，调用者不应释放
 */
XIODevice* XPlcDevice_device(const XPlcDevice* dev);

/******************************************************************************************
 * 信号接口
 ******************************************************************************************/

 /**
  * @brief 发射错误发生信号
  * @param dev XPlcDevice实例指针（非NULL）
  * @param error 发生的错误码
  * @note 连接到此信号可以监听设备错误
  */
void* XPlcDevice_errorOccurred_signal(XPlcDevice* dev, XPlcDevice_Error error);

/**
 * @brief 发射状态改变信号
 * @param dev XPlcDevice实例指针（非NULL）
 * @param state 新状态（含握手完成进入 ConnectedState）
 * @note 连接到此信号可以监听设备状态变化
 */
void* XPlcDevice_stateChanged_signal(XPlcDevice* dev, XPlcDevice_State state);

/******************************************************************************************
 * 内存管理宏
 ******************************************************************************************/

 /**
  * @brief 析构函数（延迟删除）
  * @param obj XPlcDevice实例指针
  * @note 将对象加入待删除队列，在事件循环中删除
  */
#define XPlcDevice_deleteLater   XObject_deleteLater

/**
 * @brief 析构函数（延迟反初始化）
 * @param obj XPlcDevice实例指针
 * @note 将对象加入待删除队列，在事件循环中反初始化
 */
#define XPlcDevice_deinitLater   XObject_deinitLater

#endif /* XPLC_CORE_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XPlcDevice_create
#define XPlcDevice_create() XPlcDevice_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XPLCDEVICE_H
