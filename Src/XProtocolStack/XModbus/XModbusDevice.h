#ifndef XMODBUSDEVICE_H
#define XMODBUSDEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "XObject.h"
#include "XVariant.h" // For connection parameters
#include "XString.h"  // For error string

/******************************************************************************************
 * @file XModbusDevice.h
 * @brief Modbus设备核心头文件（纯C风格，严格对齐Qt6 QModbusDevice接口）
 * @details 该文件实现了Qt6 QModbusDevice类的纯C等价封装，继承XObject基类实现面向对象特性，
 * 支持串口/网络Modbus设备的连接管理、参数配置、状态/错误管理。
 * @note 此为抽象基类，必须由子类（如XModbusClient, XModbusRtuSerialMaster）实现 open/close。
 ******************************************************************************************/

 // =============== 虚函数表枚举 (定义 Open/Close 虚函数) ===============
XCLASS_DEFINE_BEGING(XModbusDevice)
XCLASS_DEFINE_ENUM(XModbusDevice, Open) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XModbusDevice, Close),
XCLASS_DEFINE_END(XModbusDevice)

/**
 * @brief Modbus设备错误类型枚举
 */
    typedef enum {
    XModbusDevice_NoError = 0,
    XModbusDevice_ReadError,
    XModbusDevice_WriteError,
    XModbusDevice_ConnectionError,
    XModbusDevice_ConfigurationError,
    XModbusDevice_TimeoutError,
    XModbusDevice_ProtocolError,
    XModbusDevice_ReplyAbortedError,
    XModbusDevice_UnknownError,
    XModbusDevice_InvalidResponseError
} XModbusDevice_Error;

/**
 * @brief Modbus设备连接状态枚举
 */
typedef enum {
    XModbusDevice_UnconnectedState = 0,
    XModbusDevice_ConnectingState,
    XModbusDevice_ConnectedState,
    XModbusDevice_ClosingState
} XModbusDevice_State;

/**
 * @brief Modbus设备连接参数枚举
 */
typedef enum {
    // 串口Modbus相关参数（RTU模式）
    XModbusDevice_SerialPortNameParameter = 0,
    XModbusDevice_SerialParityParameter,
    XModbusDevice_SerialBaudRateParameter,
    XModbusDevice_SerialDataBitsParameter,
    XModbusDevice_SerialStopBitsParameter,
    // 网络Modbus相关参数（TCP模式）
    XModbusDevice_NetworkPortParameter,
    XModbusDevice_NetworkAddressParameter
} XModbusDevice_ConnectionParameter;

/**
 * @brief Modbus设备中间错误类型枚举
 */
typedef enum {
    XModbusDevice_ResponseCrcError = 0,
    XModbusDevice_ResponseRequestMismatch
} XModbusDevice_IntermediateError;

// 前置声明
typedef struct XModbusDevicePrivate XModbusDevicePrivate;

/**
 * @struct XModbusDevice
 * @brief Modbus设备核心结构体（纯C面向对象封装，继承XObject基类）
 * @details 这是一个抽象基类，不能直接实例化。必须由子类实现 open() 和 close() 虚函数。
 */
typedef struct XModbusDevice {
    XObject m_class; ///< 继承自XObject基类
    XModbusDevice_State m_state; ///< 当前设备连接状态
    XModbusDevice_Error m_error; ///< 当前错误码
    XString* m_errorString;      ///< 错误描述字符串
    XModbusDevicePrivate* m_d;   ///< 内部私有数据
} XModbusDevice;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/
XVtable* XModbusDevice_class_init();
XModbusDevice* XModbusDevice_create();
void XModbusDevice_init(XModbusDevice* dev);

/******************************************************************************************
 * Public API (严格对齐 QModbusDevice)
 ******************************************************************************************/

 // --- 连接参数 ---
XVariant* XModbusDevice_connectionParameter(const XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter);
void XModbusDevice_setConnectionParameter(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value);

// --- 连接管理 ---
bool XModbusDevice_connectDevice(XModbusDevice* dev);
void XModbusDevice_disconnectDevice(XModbusDevice* dev);

// --- 状态/错误查询 ---
XModbusDevice_State XModbusDevice_state(const XModbusDevice* dev);
XModbusDevice_Error XModbusDevice_error(const XModbusDevice* dev);
XString* XModbusDevice_errorString(const XModbusDevice* dev); // Caller must free the returned XString*

XIODevice* XModbusDevice_device(const XModbusDevice* dev);

// --- 受保护的 API (供子类使用) ---
void XModbusDevice_setState(XModbusDevice* dev, XModbusDevice_State newState);
void XModbusDevice_setError(XModbusDevice* dev, const char* errorText, XModbusDevice_Error error);

// --- 纯虚函数 (必须由子类实现) ---
bool XModbusDevice_open_base(XModbusDevice* dev);  // Implemented via virtual table as EXModbusDevice_Open
void XModbusDevice_close_base(XModbusDevice* dev); // Implemented via virtual table as EXModbusDevice_Close

/******************************************************************************************
 * 信号接口
 ******************************************************************************************/
void* XModbusDevice_errorOccurred_signal(XModbusDevice* dev, XModbusDevice_Error error);
void* XModbusDevice_stateChanged_signal(XModbusDevice* dev, XModbusDevice_State state);

#endif // XMODBUSDEVICE_H