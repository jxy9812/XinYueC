#ifndef XMODBUSDEVICE_H
#define XMODBUSDEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "XObject.h"

/******************************************************************************************
 * @file XModbusDevice.h
 * @brief Modbus设备核心头文件（纯C风格，对齐Qt6 QModbusDevice接口）
 * @details 该文件实现了Qt6 QModbusDevice类的纯C等价封装，继承XObject基类实现面向对象特性，
 *          支持串口/网络Modbus设备的连接管理、参数配置、状态/错误管理
 * @note 依赖XObject.h（基类）、XVariant.h（通用参数类型）、XString.h（字符串类型）、XIODeviceBase.h（IO设备基类）
 ******************************************************************************************/

 /**
  * @brief Modbus设备错误类型枚举
  * @details 标识Modbus设备在连接、读写、协议交互过程中产生的错误类型
  */
typedef enum {
    XModbusDevice_NoError = 0,          ///< 无错误（初始状态/操作成功）
    XModbusDevice_ReadError,           ///< 读操作错误（读取寄存器数据失败）
    XModbusDevice_WriteError,          ///< 写操作错误（写入寄存器数据失败）
    XModbusDevice_ConnectionError,     ///< 连接错误（串口/网络无法建立连接）
    XModbusDevice_ConfigurationError,  ///< 配置错误（参数非法，如波特率/端口号无效）
    XModbusDevice_TimeoutError,        ///< 超时错误（请求发送后未在指定时间内收到响应）
    XModbusDevice_ProtocolError,       ///< 协议错误（响应帧不符合Modbus协议规范）
    XModbusDevice_ReplyAbortedError,   ///< 响应被中止错误（接收响应过程中主动中断）
    XModbusDevice_UnknownError,        ///< 未知错误（无法归类的异常）
    XModbusDevice_InvalidResponseError ///< 无效响应错误（响应帧格式正确但内容非法）
} XModbusDevice_Error;

/**
 * @brief Modbus设备连接状态枚举
 * @details 标识Modbus设备的生命周期状态，用于判断设备是否可进行数据交互
 */
typedef enum {
    XModbusDevice_UnconnectedState = 0, ///< 未连接状态（初始状态，设备未初始化/已断开）
    XModbusDevice_ConnectingState,      ///< 连接中状态（正在尝试建立串口/网络连接）
    XModbusDevice_ConnectedState,       ///< 已连接状态（连接成功，可进行读写操作）
    XModbusDevice_ClosingState          ///< 关闭中状态（正在断开串口/网络连接）
} XModbusDevice_State;

/**
 * @brief Modbus设备连接参数枚举
 * @details 标识串口/网络Modbus设备的配置参数类型，用于Set/Get连接参数
 */
typedef enum {
    // 串口Modbus相关参数（RTU模式）
    XModbusDevice_SerialPortNameParameter = 0,  ///< 串口名称（如Windows:COM1，Linux:/dev/ttyUSB0）
    XModbusDevice_SerialParityParameter,        ///< 串口校验位（奇校验/偶校验/无校验，对应XSerialParity枚举）
    XModbusDevice_SerialBaudRateParameter,      ///< 串口波特率（如9600、19200、38400等）
    XModbusDevice_SerialDataBitsParameter,      ///< 串口数据位（7/8位）
    XModbusDevice_SerialStopBitsParameter,      ///< 串口停止位（1/2位）

    // 网络Modbus相关参数（TCP模式）
    XModbusDevice_NetworkPortParameter,         ///< 网络端口号（Modbus TCP默认502）
    XModbusDevice_NetworkAddressParameter       ///< 网络地址（IP地址，如"192.168.1.100"）
} XModbusDevice_ConnectionParameter;

/**
 * @brief Modbus设备中间错误类型枚举
 * @details 标识通信过程中的临时/非致命错误（仅用于调试，不改变设备整体错误状态）
 */
typedef enum {
    XModbusDevice_ResponseCrcError = 0,        ///< 响应CRC校验错误（接收帧CRC与计算值不一致）
    XModbusDevice_ResponseRequestMismatch      ///< 响应与请求不匹配（响应帧的功能码/地址与请求不符）
} XModbusDevice_IntermediateError;

/**
 * @brief 前置声明已实现的XModbusDataUnit结构体
 * @details 保证与Modbus数据单元的类型兼容，支持后续读写寄存器数据的接口对接
 */
typedef struct XModbusDataUnit XModbusDataUnit;
typedef struct XModbusDevicePrivate XModbusDevicePrivate;
/******************************************************************************************
 * @struct XModbusDevice
 * @brief Modbus设备核心结构体（纯C面向对象封装，继承XObject基类）
 * @details 对齐已实现的XModbusDataUnit风格，封装设备状态、错误信息、IO句柄等核心数据，
 *          支持串口/网络两种Modbus通信模式，兼容Qt6 QModbusDevice核心语义
 ******************************************************************************************/
typedef struct XModbusDevice
{
    XObject m_class;                  ///< 继承自XObject基类（实现面向对象核心特性：虚函数表、析构等）
    XModbusDevice_State m_state;      ///< 当前设备连接状态（只读，通过XModbusDevice_state()获取）
    XModbusDevice_Error m_error;      ///< 当前错误码（只读，通过XModbusDevice_error()获取）
    XString* error_string;               ///< 错误描述字符串
    XIODeviceBase* m_io_device;       ///< IO设备句柄（串口/网络IO设备基类指针，子类实现具体逻辑）
    XModbusDevicePrivate* m_d;          //内部数据
} XModbusDevice;

/******************************************************************************************
 * 类初始化/实例创建接口（XModbus系列纯C面向对象范式）
 ******************************************************************************************/

 /**
  * @brief 初始化XModbusDevice的虚函数表
  * @return 成功返回初始化后的XVtable指针，失败返回NULL
  * @details 重载XObject基类的虚函数（拷贝、析构等），实现XModbusDevice的面向对象特性，
  *          需在程序启动时调用一次，或由XModbusDevice_create()自动调用
  */
XVtable* XModbusDevice_class_init();

/**
 * @brief 创建XModbusDevice实例（动态内存分配+初始化）
 * @return 成功返回XModbusDevice实例指针，失败返回NULL
 * @details 封装XModbusDevice_class_init()和XModbusDevice_init()，一键创建可用实例，
 *          初始状态：m_state=UnconnectedState，m_error=NoError，error_string=NULL，m_io_device=NULL
 */
XModbusDevice* XModbusDevice_create();

/**
 * @brief 初始化XModbusDevice实例（仅初始化，不分配内存）
 * @param dev 待初始化的XModbusDevice实例指针（必须非NULL）
 * @details 初始化基类XObject，重置m_state/m_error为默认值，清空error_string和m_io_device
 */
void XModbusDevice_init(XModbusDevice* dev);

/******************************************************************************************
 * 连接参数配置接口
 ******************************************************************************************/

 /**
  * @brief 获取指定的连接参数值
  * @param dev XModbusDevice实例指针（必须非NULL）
  * @param parameter 要查询的参数类型（XModbusDevice_ConnectionParameter枚举值）
  * @return 成功返回XVariant类型的参数值指针，失败/无该参数返回NULL
  * @note 返回的XVariant实例需由调用者手动释放（调用XVariant_destroy()）
  */
XVariant* XModbusDevice_connectionParameter(const XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter);

/**
 * @brief 设置连接参数值
 * @param dev XModbusDevice实例指针（必须非NULL）
 * @param parameter 要设置的参数类型（XModbusDevice_ConnectionParameter枚举值）
 * @param value 要设置的参数值（XVariant类型指针，必须非NULL）
 * @details 覆盖原有参数值（如有），支持串口/网络参数的动态配置，
 *          需在connectDevice()之前调用，否则配置不生效
 */
void XModbusDevice_setConnectionParameter(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value);

/******************************************************************************************
 * 设备连接管理接口
 ******************************************************************************************/

 /**
  * @brief 连接Modbus设备（串口/网络）
  * @param dev XModbusDevice实例指针（必须非NULL）
  * @return 成功返回true，失败返回false
  * @details 根据已配置的连接参数（串口名称/IP地址等）建立连接，
  *          连接过程中会将m_state设置为ConnectingState，成功则改为ConnectedState，失败则改为UnconnectedState
  */
bool XModbusDevice_connectDevice(XModbusDevice* dev);

/**
 * @brief 断开Modbus设备连接
 * @param dev XModbusDevice实例指针（必须非NULL）
 * @details 关闭串口/网络IO设备，释放相关资源，将m_state设置为ClosingState，完成后改为UnconnectedState
 */
void XModbusDevice_disconnectDevice(XModbusDevice* dev);

/******************************************************************************************
 * 状态/错误查询接口
 ******************************************************************************************/

 /**
  * @brief 获取当前设备连接状态
  * @param dev XModbusDevice实例指针（必须非NULL）
  * @return 当前设备状态（XModbusDevice_State枚举值）
  * @note 线程安全，可在任意时机调用，用于判断设备是否可进行读写操作
  */
XModbusDevice_State XModbusDevice_state(const XModbusDevice* dev);

/**
 * @brief 获取当前错误码
 * @param dev XModbusDevice实例指针（必须非NULL）
 * @return 当前错误码（XModbusDevice_Error枚举值）
 */
XModbusDevice_Error XModbusDevice_error(const XModbusDevice* dev);

/**
 * @brief 获取错误描述字符串
 * @param dev XModbusDevice实例指针（必须非NULL）
 * @return 成功返回XString类型的错误描述，无错误返回空字符串
 * @note 返回的XString实例需由调用者手动释放
 */
XString* XModbusDevice_errorString(const XModbusDevice* dev);

/******************************************************************************************
 * IO设备句柄查询接口（对应Qt6 QModbusDevice::device）
 ******************************************************************************************/

 /**
  * @brief 获取IO设备句柄
  * @param dev XModbusDevice实例指针（必须非NULL）
  * @return 成功返回XIODeviceBase基类指针，失败返回NULL
  * @details 可强转为具体的XSerialPort/XSocket子类指针，用于底层IO操作
  */
XIODeviceBase* device(const XModbusDevice* dev);

/******************************************************************************************
 * 信号接口
 ******************************************************************************************/

 /**
  * @brief 触发错误发生信号
  * @param dev XModbusDevice实例指针（必须非NULL）
  * @param error 触发的错误类型（XModbusDevice_Error枚举值）
  * @return 成功返回信号触发上下文指针，失败返回NULL
  * @details 内部会更新m_error和error_string，并调用注册的错误回调函数
  */
void* XModbusDevice_errorOccurred_signal(XModbusDevice* dev, XModbusDevice_Error error);

/**
 * @brief 触发状态变更信号
 * @param dev XModbusDevice实例指针（必须非NULL）
 * @param state 变更后的设备状态（XModbusDevice_State枚举值）
 * @return 成功返回信号触发上下文指针，失败返回NULL
 * @details 内部会更新m_state，并调用注册的状态回调函数
 */
void* XModbusDevice_stateChanged_signal(XModbusDevice* dev, XModbusDevice_State state);

#endif // XMODBUSDEVICE_H