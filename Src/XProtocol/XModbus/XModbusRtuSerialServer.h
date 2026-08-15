#ifndef XMODBUSRTUSERIALSERVER_H
#define XMODBUSRTUSERIALSERVER_H

#include "XModbusServer.h"
#include "XModbusPdu.h"
#include "XSerialPort.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XModbusRtuSerialServer.h
 * @brief Modbus RTU串口服务器（对齐Qt6 QModbusRtuSerialServer）
 * @details 实现基于串口通信的Modbus RTU从站功能
 *
 * @par 功能特性
 * - 支持标准Modbus RTU协议（地址码 + PDU + CRC）
 * - 自动CRC16校验
 * - 支持广播地址（0x00）
 * - 自动计算帧间延迟（基于波特率）
 * - 半双工模式，同一时间只处理一个请求
 *
 * @par 协议说明
 * Modbus RTU帧结构：
 * | 字节 | 长度 | 说明 |
 * |------|------|------|
 * | 0    | 1    | 从站地址 |
 * | 1    | 1    | 功能码 |
 * | 2    | N    | 数据 |
 * | 最后2 | 2   | CRC16校验（小端序） |
 */

XCLASS_DEFINE_BEGING(XModbusRtuSerialServer)
XCLASS_DEFINE_EXTEND_END(XModbusRtuSerialServer, XModbusServer)

/**
 * @brief Modbus RTU串口服务器结构体
 * @details 继承自XModbusServer，实现串口通信的从站功能
 *
 * @par 成员说明
 * | 成员 | 类型 | 说明 |
 * |------|------|------|
 * | m_base | XModbusServer | 基类，继承自XModbusServer |
 * | m_serialPort | XSerialPort* | 串口对象 |
 * | m_interFrameDelay | int | 帧间延迟（微秒），0表示自动计算 |
 * | m_turnaroundDelay | int | 响应延迟（毫秒） |
 * | m_receiveBuffer | XByteArray* | 接收缓冲区 |
 * | m_interFrameTimer | XTimerId | 帧间延迟定时器ID |
 * | m_receiveBufferTimer | XTimerId | 接收缓冲区定时器（用于帧超时检测） |
 */
typedef struct XModbusRtuSerialServer {
    XModbusServer m_base;               ///< 继承自XModbusServer
    XSerialPort* m_serialPort;          ///< 串口对象
    int m_interFrameDelay;              ///< 帧间延迟（微秒），0表示自动计算
    int m_turnaroundDelay;              ///< 响应延迟（毫秒）
    XByteArray* m_receiveBuffer;        ///< 接收缓冲区
    XTimerId m_interFrameTimer;         ///< 帧间延迟定时器ID
    XTimerId m_receiveBufferTimer;      ///< 接收缓冲区定时器（用于帧超时检测）
    XTimerId m_turnaroundTimer;         ///< 响应延迟定时器ID
} XModbusRtuSerialServer;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

/**
 * @brief 初始化XModbusRtuSerialServer的虚函数表
 * @return 初始化完成的虚函数表指针
 * @note 该函数是线程安全的，多次调用返回同一虚表实例
 */
XVtable* XModbusRtuSerialServer_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XModbusRtuSerialServer实例
 * @return 成功返回指向新分配XModbusRtuSerialServer对象的指针，失败返回NULL
 * @note 返回的对象必须通过 XObject_deleteLater 释放
 * @par 内存管理
 * - 创建时会自动创建关联的XSerialPort对象
 * - 删除时会自动释放关联的XSerialPort对象
 */
XModbusRtuSerialServer* XModbusRtuSerialServer_create_ex(XMemoryType memory);

/**
 * @brief 初始化一个已分配的XModbusRtuSerialServer实例
 * @param server 待初始化的XModbusRtuSerialServer对象指针（非NULL）
 * @note 该函数会初始化基类成员、创建串口对象、设置默认参数
 * @par 默认参数
 * - 帧间延迟：自动计算（基于波特率）
 * - 响应延迟：100毫秒
 * - 服务器地址：1
 */
/**
 * @brief 初始化RTU串行从站实例
 * @param server 待初始化的从站指针（非NULL）
 */
void XModbusRtuSerialServer_init(XModbusRtuSerialServer* server);

/******************************************************************************************
 * 串口访问接口
 ******************************************************************************************/

/**
 * @brief 获取关联的串口对象
 * @param server XModbusRtuSerialServer实例指针
 * @return 串口对象指针，server为NULL或串口未创建时返回NULL
 * @note 返回的串口对象由XModbusRtuSerialServer管理，不需要手动释放
 * @par 使用方法
 * @code
 * XModbusRtuSerialServer* server = XModbusRtuSerialServer_create();
 * XSerialPort* port = XModbusRtuSerialServer_serialPort(server);
 * XSerialPort_setPortName(port, "/dev/ttyS0");
 * XSerialPort_setBaudRate(port, 9600);
 * @endcode
 */
XSerialPort* XModbusRtuSerialServer_serialPort(const XModbusRtuSerialServer* server);

/******************************************************************************************
 * 帧间延迟/响应延迟接口
 ******************************************************************************************/

/**
 * @brief 获取帧间延迟（微秒）
 * @param server XModbusRtuSerialServer实例指针
 * @return 帧间延迟（微秒），0表示自动计算，-1表示出错
 * @note 帧间延迟用于RTU协议中区分两个连续帧的最小间隔时间
 *       自动计算时基于串口波特率：3.5个字符传输时间
 */
int XModbusRtuSerialServer_interFrameDelay(const XModbusRtuSerialServer* server);

/**
 * @brief 设置帧间延迟（微秒）
 * @param server XModbusRtuSerialServer实例指针（非NULL）
 * @param microseconds 帧间延迟（微秒），传入0表示自动计算
 * @note 自动计算规则：基于波特率的3.5个字符传输时间
 *       波特率 <= 19200: 固定1750微秒
 *       波特率 > 19200: 按3.5个字符时间计算
 */
/**
 * @brief 设置帧间延迟（微秒）
 * @param server 从站指针（非NULL）
 * @param microseconds 帧间延迟（微秒），-1或小于自动计算值时使用自动计算值
 * @note 自动计算值基于波特率：38500000/baudRate，最小1750微秒
 */
void XModbusRtuSerialServer_setInterFrameDelay(XModbusRtuSerialServer* server, int microseconds);

/******************************************************************************************
 * 继承自父类的API（使用宏定义）
 ******************************************************************************************/

#define XModbusRtuSerialServer_serverAddress            XModbusServer_serverAddress
#define XModbusRtuSerialServer_setServerAddress         XModbusServer_setServerAddress
#define XModbusRtuSerialServer_data1                    XModbusServer_data1
#define XModbusRtuSerialServer_setData1                 XModbusServer_setData1
#define XModbusRtuSerialServer_setData2                 XModbusServer_setData2
#define XModbusRtuSerialServer_data2                    XModbusServer_data2
#define XModbusRtuSerialServer_setMap_base              XModbusServer_setMap_base
#define XModbusRtuSerialServer_setMap_move_base         XModbusServer_setMap_move_base
#define XModbusRtuSerialServer_setMap_ref_base          XModbusServer_setMap_ref_base
#define XModbusRtuSerialServer_value_base               XModbusServer_value_base
#define XModbusRtuSerialServer_value_const_base         XModbusServer_value_const_base
#define XModbusRtuSerialServer_setValue_base            XModbusServer_setValue_base
#define XModbusRtuSerialServer_setValue_move_base       XModbusServer_setValue_move_base
#define XModbusRtuSerialServer_processesBroadcast_base  XModbusServer_processesBroadcast_base
#define XModbusRtuSerialServer_dataWritten_signal       XModbusServer_dataWritten_signal

/******************************************************************************************
 * 设备操作接口（虚函数）
 ******************************************************************************************/

/**
 * @brief 打开设备（虚函数）
 * @param device XModbusRtuSerialServer实例指针（转换为XModbusDevice*）
 * @return 成功返回true，失败返回false
 * @par 说明
 * - 打开串口并连接信号
 * - 打开成功后设备状态变为XModbusDevice_ConnectedState
 */
#define XModbusRtuSerialServer_open_base         XModbusDevice_open_base

/**
 * @brief 关闭设备（虚函数）
 * @param device XModbusRtuSerialServer实例指针（转换为XModbusDevice*）
 * @par 说明
 * - 关闭串口并断开信号连接
 * - 关闭后设备状态变为XModbusDevice_UnconnectedState
 */
#define XModbusRtuSerialServer_close_base        XModbusDevice_close_base

/******************************************************************************************
 * 内存管理宏
 ******************************************************************************************/

#define XModbusRtuSerialServer_deinitLater          XModbusServer_deinitLater
#define XModbusRtuSerialServer_deleteLater          XModbusServer_deleteLater

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XModbusRtuSerialServer_create
#define XModbusRtuSerialServer_create() XModbusRtuSerialServer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XMODBUSRTUSERIALSERVER_H


