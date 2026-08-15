#ifndef XMODBUSDEVICE_H
#define XMODBUSDEVICE_H

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

/**
 * @file XModbusDevice.h
 * @brief Modbus设备基类（对齐Qt6 QModbusDevice）
 * @details 实现Modbus设备的公共接口，包括连接管理、参数配置、状态/错误管理等
 *
 * @par 功能特性
 * - 设备状态管理（未连接、连接中、已连接、关闭中）
 * - 错误处理机制
 * - 连接参数配置（串口/网络参数）
 * - 信号机制（状态改变、错误发生）
 *
 * @par 使用示例
 * @code
 * // 子类使用示例（以RTU客户端为例）
 * XModbusRtuSerialClient* client = XModbusRtuSerialClient_create();
 *
 * // 设置连接参数
 * XModbusDevice_setConnectionParameter((XModbusDevice*)client,
 *     XModbusDevice_SerialPortNameParameter, XVariant_create_string("COM1"));
 * XModbusDevice_setConnectionParameter((XModbusDevice*)client,
 *     XModbusDevice_SerialBaudRateParameter, XVariant_create_int(9600));
 *
 * // 连接设备
 * if (XModbusDevice_connectDevice((XModbusDevice*)client)) {
 *     // 连接成功，可以进行通信
 * }
 *
 * // 断开连接
 * XModbusDevice_disconnectDevice((XModbusDevice*)client);
 *
 * // 清理
 * XModbusDevice_deleteLater((XModbusDevice*)client);
 * @endcode
 *
 * @note 此为抽象基类，必须由子类（如XModbusClient, XModbusRtuSerialClient）实现 open/close
 */

 /******************************************************************************************
  * 虚函数表枚举定义
  ******************************************************************************************/

XCLASS_DEFINE_BEGING(XModbusDevice)
XCLASS_DEFINE_ENUM(XModbusDevice, Open) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XModbusDevice, Close),
XCLASS_DEFINE_END(XModbusDevice)

/******************************************************************************************
 * 枚举类型定义
 ******************************************************************************************/

 /**
  * @brief Modbus设备错误类型枚举
  * @details 对齐Qt6 QModbusDevice::Error
  */
typedef enum {
    XModbusDevice_NoError = 0,              ///< 无错误
    XModbusDevice_ReadError,                ///< 读取错误
    XModbusDevice_WriteError,               ///< 写入错误
    XModbusDevice_ConnectionError,          ///< 连接错误
    XModbusDevice_ConfigurationError,       ///< 配置错误
    XModbusDevice_TimeoutError,             ///< 超时错误
    XModbusDevice_ProtocolError,            ///< 协议错误
    XModbusDevice_ReplyAbortedError,        ///< 响应被中止
    XModbusDevice_UnknownError,             ///< 未知错误
    XModbusDevice_InvalidResponseError      ///< 无效响应
} XModbusDevice_Error;

/**
 * @brief Modbus设备连接状态枚举
 * @details 对齐Qt6 QModbusDevice::State
 */
typedef enum {
    XModbusDevice_UnconnectedState = 0,     ///< 未连接状态
    XModbusDevice_ConnectingState,          ///< 连接中状态
    XModbusDevice_ConnectedState,           ///< 已连接状态
    XModbusDevice_ClosingState              ///< 关闭中状态
} XModbusDevice_State;

/**
 * @brief Modbus设备连接参数枚举
 * @details 对齐Qt6 QModbusDevice::ConnectionParameter
 * @par 参数说明
 * | 参数 | 类型 | 说明 |
 * |------|------|------|
 * | SerialPortNameParameter | string | 串口名称，如"COM1"或"/dev/ttyUSB0" |
 * | SerialParityParameter | int | 校验位，XSerialPort_Parity枚举值 |
 * | SerialBaudRateParameter | int | 波特率，如9600、19200、115200 |
 * | SerialDataBitsParameter | int | 数据位，XSerialPort_DataBits枚举值 |
 * | SerialStopBitsParameter | int | 停止位，XSerialPort_StopBits枚举值 |
 * | NetworkPortParameter | int | 网络端口号 |
 * | NetworkAddressParameter | string | 网络地址，如"192.168.1.1" |
 */
typedef enum {
    // 串口Modbus相关参数（RTU模式）
    XModbusDevice_SerialPortNameParameter = 0,  ///< 串口名称参数
    XModbusDevice_SerialParityParameter,        ///< 串口校验位参数
    XModbusDevice_SerialBaudRateParameter,      ///< 串口波特率参数
    XModbusDevice_SerialDataBitsParameter,      ///< 串口数据位参数
    XModbusDevice_SerialStopBitsParameter,      ///< 串口停止位参数
    // 网络Modbus相关参数（TCP模式）
    XModbusDevice_NetworkPortParameter,         ///< 网络端口参数
    XModbusDevice_NetworkAddressParameter,      ///< 网络地址参数
    // 参数数量
    XModbusDevice_ParameterCount                ///< 参数总数
} XModbusDevice_ConnectionParameter;

/**
 * @brief Modbus设备中间错误类型枚举
 * @details 用于内部错误检测
 */
typedef enum {
    XModbusDevice_ResponseCrcError = 0,     ///< 响应CRC校验错误
    XModbusDevice_ResponseRequestMismatch   ///< 响应与请求不匹配
} XModbusDevice_IntermediateError;

/******************************************************************************************
 * 结构体定义
 ******************************************************************************************/

 /**
  * @brief Modbus设备基类结构体
  * @details 继承自XObject，提供Modbus设备的公共接口
  *
  * @par 成员说明
  * | 成员 | 类型 | 说明 |
  * |------|------|------|
  * | m_class | XObject | 基类，继承自XObject |
  * | m_state | XModbusDevice_State | 当前设备连接状态 |
  * | m_error | XModbusDevice_Error | 当前错误码 |
  * | m_errorString | XString* | 错误描述字符串 |
  * | m_params | XVariant*[] | 连接参数数组 |
  * | m_ioDevice | XIODevice* | 底层IO设备 |
  */
typedef struct XModbusDevice {
    XObject m_class;                                      ///< 继承自XObject基类
    uint16_t/*XModbusDevice_State*/ m_state;                          ///< 当前设备连接状态
    uint16_t/*XModbusDevice_Error*/ m_error;                          ///< 当前错误码
    XString* m_errorString;                               ///< 错误描述字符串
    XVariant* m_params[XModbusDevice_ParameterCount];     ///< 连接参数数组
    XIODevice* m_ioDevice;                                ///< 底层IO设备
} XModbusDevice;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

 /**
  * @brief 初始化XModbusDevice的虚函数表
  * @return 初始化完成的虚函数表指针
  * @note 该函数是线程安全的，多次调用返回同一虚表实例
  */
XVtable* XModbusDevice_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XModbusDevice实例
 * @return 成功返回指向新分配XModbusDevice对象的指针，失败返回NULL
 * @note 返回的对象必须通过 XObject_deleteLater 释放
 * @warning 此为抽象基类，通常不应直接创建实例，而应使用子类的创建函数
 */
XModbusDevice* XModbusDevice_create_ex(XMemoryType memory);

/**
 * @brief 初始化一个已分配的XModbusDevice实例
 * @param dev 待初始化的XModbusDevice对象指针（非NULL）
 * @note 该函数会初始化基类成员、设置默认状态和错误值
 * @par 默认值
 * - 状态：XModbusDevice_UnconnectedState
 * - 错误：XModbusDevice_NoError
 * - 参数：全部为NULL
 * - IO设备：NULL
 */
/**
 * @brief 初始化设备实例
 * @param dev 待初始化的设备指针（非NULL）
 */
void XModbusDevice_init(XModbusDevice* dev);

/******************************************************************************************
 * 连接参数接口
 ******************************************************************************************/

 /**
  * @brief 获取连接参数值
  * @param dev XModbusDevice实例指针
  * @param parameter 参数类型（XModbusDevice_ConnectionParameter枚举）
  * @return 成功返回参数值的XVariant副本，失败返回NULL
  * @note 调用者负责释放返回的XVariant对象
  * @par 使用示例
  * @code
  * XVariant* portName = XModbusDevice_connectionParameter(device, XModbusDevice_SerialPortNameParameter);
  * if (portName) {
  *     XString* str = XVariant_toString(portName);
  *     // 使用 str...
  *     XString_delete_base(str);
  *     XVariant_delete_base(portName);
  * }
  * @endcode
  */
XVariant* XModbusDevice_connectionParameter(const XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter);
const XVariant* XModbusDevice_connectionParameter_const(const XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter);
/**
 * @brief 设置连接参数值
 * @param dev XModbusDevice实例指针（非NULL）
 * @param parameter 参数类型（XModbusDevice_ConnectionParameter枚举）
 * @param value 参数值（XVariant指针，会被复制）
 * @note 参数值会被内部复制，调用者可以安全释放原始value
 * @par 使用示例
 * @code
 * XModbusDevice_setConnectionParameter(device,
 *     XModbusDevice_SerialPortNameParameter, XVariant_create_string("COM1"));
 * XModbusDevice_setConnectionParameter(device,
 *     XModbusDevice_SerialBaudRateParameter, XVariant_create_int(9600));
 * @endcode
 */
/**
 * @brief 设置连接参数（深拷贝）
 * @param dev 设备指针（非NULL）
 * @param parameter 参数类型
 * @param value 参数值（深拷贝）
 */
void XModbusDevice_setConnectionParameter(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value);
/**
 * @brief 设置连接参数（移动语义）
 * @param dev 设备指针（非NULL）
 * @param parameter 参数类型
 * @param value 参数值（移动，函数内释放原指针）
 */
void XModbusDevice_setConnectionParameter_move(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value);
/**
 * @brief 设置连接参数（引用语义）
 * @param dev 设备指针（非NULL）
 * @param parameter 参数类型
 * @param value 参数值（引用，函数内不释放）
 */
void XModbusDevice_setConnectionParameter_ref(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value);
/******************************************************************************************
 * 连接管理接口
 ******************************************************************************************/

 /**
  * @brief 连接设备
  * @param dev XModbusDevice实例指针（非NULL）
  * @return 成功返回true，失败返回false
  * @note 该函数通过虚函数表调用子类实现的open()函数
  * @par 连接流程
  * 1. 检查当前状态是否允许连接
  * 2. 读取连接参数配置
  * 3. 调用子类的open()实现
  * 4. 成功则状态变为XModbusDevice_ConnectedState
  */
/**
 * @brief 连接设备
 * @param dev 设备指针（非NULL）
 * @return 连接成功返回true，失败返回false
 * @note 内部调用open虚函数，设置设备状态为ConnectedState
 */
bool XModbusDevice_connectDevice(XModbusDevice* dev);

/**
 * @brief 断开设备连接
 * @param dev XModbusDevice实例指针（非NULL）
 * @note 该函数通过虚函数表调用子类实现的close()函数
 * @par 断开流程
 * 1. 调用子类的close()实现
 * 2. 状态变为XModbusDevice_UnconnectedState
 */
void XModbusDevice_disconnectDevice(XModbusDevice* dev);

/******************************************************************************************
 * 状态/错误查询接口
 ******************************************************************************************/

 /**
  * @brief 获取设备当前状态
  * @param dev XModbusDevice实例指针
  * @return 设备状态，dev为NULL时返回XModbusDevice_UnconnectedState
  */
XModbusDevice_State XModbusDevice_state(const XModbusDevice* dev);

/**
 * @brief 获取设备当前错误码
 * @param dev XModbusDevice实例指针
 * @return 错误码，dev为NULL时返回XModbusDevice_UnknownError
 */
XModbusDevice_Error XModbusDevice_error(const XModbusDevice* dev);

/**
 * @brief 获取设备错误描述字符串
 * @param dev XModbusDevice实例指针
 * @return 错误描述字符串的副本，调用者负责释放
 * @note 如果没有错误字符串，返回默认的错误描述
 */
XString* XModbusDevice_errorString(const XModbusDevice* dev);

/**
 * @brief 获取底层IO设备
 * @param dev XModbusDevice实例指针
 * @return 底层IO设备指针，dev为NULL或无IO设备时返回NULL
 * @note 返回的IO设备由XModbusDevice子类管理，调用者不应释放
 */
XIODevice* XModbusDevice_device(const XModbusDevice* dev);

/******************************************************************************************
 * 信号接口
 ******************************************************************************************/

 /**
  * @brief 发射错误发生信号
  * @param dev XModbusDevice实例指针（非NULL）
  * @param error 发生的错误码
  * @note 连接到此信号可以监听设备错误
  */
void* XModbusDevice_errorOccurred_signal(XModbusDevice* dev, XModbusDevice_Error error);

/**
 * @brief 发射状态改变信号
 * @param dev XModbusDevice实例指针（非NULL）
 * @param state 新状态
 * @note 连接到此信号可以监听设备状态变化
 */
void* XModbusDevice_stateChanged_signal(XModbusDevice* dev, XModbusDevice_State state);

/******************************************************************************************
 * 内存管理宏
 ******************************************************************************************/

 /**
  * @brief 析构函数（延迟删除）
  * @param obj XModbusDevice实例指针
  * @note 将对象加入待删除队列，在事件循环中删除
  */
#define XModbusDevice_deleteLater   XObject_deleteLater

/**
 * @brief 析构函数（延迟删除）
 * @param obj XModbusDevice实例指针
 * @note 将对象加入待删除队列，在事件循环中删除
 */
#define XModbusDevice_deinitLater   XObject_deinitLater

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XModbusDevice_create
#define XModbusDevice_create(...) XModbusDevice_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)
#undef XModbusRtuSerialClient_create
#define XModbusRtuSerialClient_create(...) XModbusRtuSerialClient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif // XMODBUSDEVICE_H

