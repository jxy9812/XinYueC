/**
 * @file       XModbusDevice.h
 * @brief      Modbus 设备公共基类：连接状态机、错误码、连接参数表
 *             （对齐 Qt6 QModbusDevice）。
 * @details    [XProtocol/XModbus] XModbusClient 系与 XModbusServer 系的
 *             公共父类，只承载三件事：①连接状态机（Unconnected/
 *             Connecting/Connected/Closing，迁移由子类 open/close 虚实现
 *             驱动）；②错误码与错误描述（error()/errorString()）；③连接
 *             参数表（串口名/波特率/校验位、网络地址/端口等，写入有
 *             copy/move/ref 三变体，设备一律持有副本）。本类不含任何 IO
 *             与协议逻辑——收发与定时器在子类实现。
 *             信号：stateChanged（状态迁移）、errorOccurred（置错），
 *             经 XObject 信号槽分发给已连接槽。
 *             线程模型：与库一致的单线程事件循环，零锁。
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
 * @note 此为抽象基类：open/close 虚槽由子类（XModbusClient、
 *       XModbusServer 派生系）重载实现，基类自身连接一律失败。
 */
#ifndef XMODBUSDEVICE_H
#define XMODBUSDEVICE_H

#include "XModbus_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_CLIENT_ON

#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"
#include "XVariant.h"
#include "XString.h"
#include "XMap.h"

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
  * @brief Modbus 设备基类结构体（继承 XObject——需要 stateChanged/
  *        errorOccurred 信号，故背 XObject 的信号机制开销）。
  * @details 成员仅供实现与子类使用，外部一律经公开 API 访问。
  */
typedef struct XModbusDevice {
    XObject m_class;                                      /**< 基类（第一个成员，由 XClass 管理，禁止手工修改）。 */
    uint16_t/*XModbusDevice_State*/ m_state;              /**< 当前连接状态（XModbusDevice_State；经 setState 迁移并广播 stateChanged）。 */
    uint16_t/*XModbusDevice_Error*/ m_error;              /**< 当前错误码（XModbusDevice_Error；NoError=无错误）。 */
    XString* m_errorString;                               /**< 错误描述文本（对象拥有，析构释放；NULL=未设置，errorString() 回退为按错误码生成默认文案）。 */
    XVariant* m_params[XModbusDevice_ParameterCount];     /**< 连接参数表（下标=XModbusDevice_ConnectionParameter；对象持有深拷贝，析构释放；NULL=该参数未设置）。 */
    XIODevice* m_ioDevice;                                /**< 底层 IO 设备（借用指针，由子类创建并释放；本类析构仅置空，不释放）。 */
} XModbusDevice;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

 /**
  * @brief 初始化XModbusDevice的虚函数表
  * @return 初始化完成的虚函数表指针
  * @note 虚表在首次调用时初始化并缓存，此后返回同一实例。
  *       本框架为单线程事件循环模型：class_init 无锁保护，
  *       须在事件循环启动前（或同一线程内）调用，通常由 create 接口自动完成。
  */
XVtable* XModbusDevice_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XModbusDevice实例
 * @param memory 内存分配器类型（XMemoryType；分配与所有权标记按同一类型登记）
 * @return 新设备对象（调用方 XModbusDevice_deleteLater 释放）；内存分配
 *         失败返回 NULL
 * @note 堆属性与分配器随对象登记，deleteLater 按登记路径释放
 * @warning 此为抽象基类，通常不应直接创建实例，而应使用子类的创建函数
 */
XModbusDevice* XModbusDevice_create_ex(XMemoryType memory);

/**
 * @brief 初始化设备实例（栈上/已分配内存；状态机与参数表复位为默认）。
 * @param dev 目标对象；NULL 时不执行任何操作，可为未初始化内存
 *        （基类与派生字段在内部完成初始化）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @par 默认值
 * - 状态：XModbusDevice_UnconnectedState
 * - 错误：XModbusDevice_NoError（错误描述为 NULL）
 * - 参数：全部为NULL
 * - IO设备：NULL
 * @note 与 XClassDeinit 成对出现：析构释放 m_errorString 与参数表；
 *       m_ioDevice 由子类管理不受影响。堆对象统一走 deleteLater 路径。
 */
void XModbusDevice_init(XModbusDevice* dev);

/******************************************************************************************
 * 连接参数接口
 ******************************************************************************************/

 /**
  * @brief 获取连接参数值的深拷贝副本。
  * @param dev XModbusDevice实例指针；NULL 返回 NULL。
  * @param parameter 参数类型（XModbusDevice_ConnectionParameter枚举）；
  *        非法下标或该参数未设置返回 NULL。
  * @return 新 XVariant 对象（调用方 XClassDelete 释放）；dev 为 NULL、
  *         parameter 非法或该参数未设置返回 NULL。
  * @note 副本与设备内部参数互不影响；零拷贝取值用
  *       connectionParameter_const。
  * @par 使用示例
  * @code
  * XVariant* portName = XModbusDevice_connectionParameter(device, XModbusDevice_SerialPortNameParameter);
  * if (portName) {
  *     XString* str = XVariant_toString(portName);
  *     // 使用 str...
  *     XClassDelete(str);
  *     XClassDelete(portName);
  * }
  * @endcode
  */
XVariant* XModbusDevice_connectionParameter(const XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter);
/**
 * @brief 获取连接参数值的只读借用引用（零拷贝）。
 * @param dev XModbusDevice实例指针；NULL 返回 NULL。
 * @param parameter 参数类型（XModbusDevice_ConnectionParameter枚举）；
 *        非法下标返回 NULL。
 * @return 参数的只读借用指针（设备内部存储，禁止释放或修改），仅在
 *         设备存续且该参数未被覆盖前有效；该参数未设置返回 NULL。
 * @note connectionParameter 的深拷贝即以本函数为源。
 */
const XVariant* XModbusDevice_connectionParameter_const(const XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter);
/**
 * @brief 设置连接参数（深拷贝：设备登记自己的副本）。
 * @param dev XModbusDevice实例指针；NULL、value 为 NULL 或 parameter
 *        非法下标时不执行任何操作，设备保持不变。
 * @param parameter 参数类型（XModbusDevice_ConnectionParameter枚举）。
 * @param value 参数值（XVariant，借用：函数内深拷贝，不取得所有权；
 *        调用方可设后立即释放原对象）。
 * @return 无。参数非法时设备保持不变。
 * @note 已有值时经 XClassCopy 原地覆盖（不新建对象），无值时创建深拷贝；
 *       副本由设备拥有，析构统一释放。
 * @par 使用示例
 * @code
 * XModbusDevice_setConnectionParameter(device,
 *     XModbusDevice_SerialPortNameParameter, XVariant_create_string("COM1"));
 * XModbusDevice_setConnectionParameter(device,
 *     XModbusDevice_SerialBaudRateParameter, XVariant_create_int(9600));
 * @endcode
 */
void XModbusDevice_setConnectionParameter(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value);
/**
 * @brief 设置连接参数（移动语义：value 的资源转入设备）。
 * @param dev XModbusDevice实例指针；NULL、value 为 NULL 或 parameter
 *        非法下标时不执行任何操作（value 所有权仍归调用方）。
 * @param parameter 参数类型（XModbusDevice_ConnectionParameter枚举）。
 * @param value 参数值（XVariant；函数内经 XClassMove/create_move 抽取
 *        其资源，返回后 value 变为空壳——内存仍由调用方按原分配方式
 *        释放，此后不得再解引用其内容）。
 * @return 无。
 * @note 已有值时原地 XClassMove 覆盖，无值时以 create_move 接管资源；
 *       设备析构释放接管后的对象。
 */
void XModbusDevice_setConnectionParameter_move(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value);
/**
 * @brief 设置连接参数（引用形态入参：设备登记自己的深拷贝）。
 * @param dev XModbusDevice实例指针；NULL、value 为 NULL 或 parameter
 *        非法下标时不执行任何操作，设备保持不变。
 * @param parameter 参数类型（XModbusDevice_ConnectionParameter枚举）。
 * @param value 参数值（XVariant，借用：设备登记自己的深拷贝；调用方
 *        在函数返回后即可释放 value，不会悬垂）。
 * @return 无。
 * @note [UAF 根修 2026-10-30] 对标 Qt6 QModbusDevice::setConnectionParameter
 *       （参数表按值存副本）：已有值先释放旧副本再登记新深拷贝。旧实现
 *       存裸指针且覆盖时删除调用方内存，调用方「set 后 delete」即 UAF；
 *       本变体与 copy 变体在调用方视角已等价。
 */
void XModbusDevice_setConnectionParameter_ref(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value);
/******************************************************************************************
 * 连接管理接口
 ******************************************************************************************/

 /**
  * @brief 连接设备（转发子类 open 虚实现）。
  * @param dev XModbusDevice实例指针；NULL 或虚表中无 open 实现返回 false。
  * @return 子类 open 实现返回 true 即 true；参数非法或抽象基类
  *         （open 未重载）返回 false。
  * @note 本函数只做虚分派：状态迁移（Connecting/Connected）、失败置错
  *       均由子类 open 实现负责；失败原因经 error()/errorString() 查询。
  */
bool XModbusDevice_connectDevice(XModbusDevice* dev);

/**
 * @brief 断开设备连接（转发子类 close 虚实现）。
 * @param dev XModbusDevice实例指针；NULL 或虚表中无 close 实现时
 *        不执行任何操作。
 * @return 无。
 * @note 本函数只做虚分派：状态回到 XModbusDevice_UnconnectedState 由
 *       子类 close 实现负责（通常经 setState 触发 stateChanged）。
 */
void XModbusDevice_disconnectDevice(XModbusDevice* dev);

/******************************************************************************************
 * 状态/错误查询接口
 ******************************************************************************************/

 /**
  * @brief 获取设备当前状态。
  * @param dev XModbusDevice实例指针。
  * @return 设备状态（XModbusDevice_State）；dev 为 NULL 返回
  *         XModbusDevice_UnconnectedState。
  */
XModbusDevice_State XModbusDevice_state(const XModbusDevice* dev);

/**
 * @brief 获取设备当前错误码。
 * @param dev XModbusDevice实例指针。
 * @return 错误码（XModbusDevice_Error）；dev 为 NULL 返回
 *         XModbusDevice_UnknownError。
 */
XModbusDevice_Error XModbusDevice_error(const XModbusDevice* dev);

/**
 * @brief 获取错误描述字符串（深拷贝副本）。
 * @param dev XModbusDevice实例指针。
 * @return 新 XString（调用方 XClassDelete 释放）：dev 为 NULL 返回
 *         "Device is NULL"；已 setError 设置过专门描述返回其副本；否则
 *         按当前错误码返回默认文案（如 "Timeout error"）。仅内存分配
 *         失败时返回 NULL。
 * @note 本函数不改动设备内部状态；设备内部的 m_errorString 生命周期
 *       归设备（析构释放），与返回副本无关。
 */
XString* XModbusDevice_errorString(const XModbusDevice* dev);

/**
 * @brief 获取底层IO设备。
 * @param dev XModbusDevice实例指针；NULL 返回 NULL。
 * @return 底层IO设备的借用指针（由子类创建与释放，禁止调用方释放或
 *         越权改写）；尚未创建返回 NULL。
 * @note 借用指针仅在设备存续期间有效。
 */
XIODevice* XModbusDevice_device(const XModbusDevice* dev);

/******************************************************************************************
 * 信号接口
 ******************************************************************************************/

 /**
  * @brief 发射错误发生信号（errorOccurred）。
  * @param dev XModbusDevice实例指针；NULL 时不发射。
  * @param error 发生的错误码（随信号参数传递给已连接槽）。
  * @return 信号标识句柄（信号函数地址，仅供框架内部使用）；调用方
  *         不需使用亦不可释放。
  * @note 置错入口 setState/setError（Protected）内部会自动发射本信号；
  *       信号经 XObject 信号槽机制分发给已连接槽。
  */
void* XModbusDevice_errorOccurred_signal(XModbusDevice* dev, XModbusDevice_Error error);

/**
 * @brief 发射状态改变信号（stateChanged）。
 * @param dev XModbusDevice实例指针；NULL 时不发射。
 * @param state 新状态（随信号参数传递给已连接槽）。
 * @return 信号标识句柄（信号函数地址，仅供框架内部使用）；调用方
 *         不需使用亦不可释放。
 * @note 状态迁移入口 setState（Protected）内部会自动发射本信号；
 *       信号经 XObject 信号槽机制分发给已连接槽。
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

#endif /* XMODBUS_CLIENT_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XModbusDevice_create
#define XModbusDevice_create() XModbusDevice_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XModbusRtuSerialClient_create
#define XModbusRtuSerialClient_create() XModbusRtuSerialClient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XMODBUSDEVICE_H

