#ifndef XMODBUSTCPSERVER_H
#define XMODBUSTCPSERVER_H

#include "XModbusServer.h"
#include "XModbusPdu.h"
#include "XTcpServer.h"
#include "XTcpSocket.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XModbusTcpServer.h
 * @brief Modbus TCP服务器（对齐Qt6 QModbusTcpServer）
 * @details 实现基于TCP/IP通信的Modbus TCP从站功能
 *
 * @par 功能特性
 * - 支持标准Modbus TCP协议（MBAP头部 + PDU）
 * - 支持多客户端并发连接
 * - 内置TCP连接观察器接口
 * - 自动管理事务标识符
 *
 * @par 协议说明
 * Modbus TCP帧结构（MBAP头部 + PDU）：
 * | 字节 | 长度 | 说明 |
 * |------|------|------|
 * | 0-1  | 2    | 事务标识符 |
 * | 2-3  | 2    | 协议标识符（固定0x0000） |
 * | 4-5  | 2    | 长度（后续字节数） |
 * | 6    | 1    | 单元标识符 |
 * | 7+   | N    | Modbus PDU |
 */

/**
 * @brief TCP连接观察器接口
 * @details 用于在建立新TCP连接时进行访问控制，对齐QModbusTcpConnectionObserver
 */
typedef struct XModbusTcpConnectionObserver {
    void* context;  ///< 用户上下文
    bool (*acceptNewConnection)(void* context, XTcpSocket* newClient);  ///< 接受/拒绝新连接
} XModbusTcpConnectionObserver;

XCLASS_DEFINE_BEGING(XModbusTcpServer)
XCLASS_DEFINE_EXTEND_END(XModbusTcpServer, XModbusServer)

/**
 * @brief Modbus TCP服务器结构体
 * @details 继承自XModbusServer，实现TCP/IP通信的从站功能
 *
 * @par 成员说明
 * | 成员 | 类型 | 说明 |
 * |------|------|------|
 * | m_base | XModbusServer | 基类，继承自XModbusServer |
 * | m_tcpServer | XTcpServer* | TCP服务器对象 |
 * | m_connectedClients | XMap* | 已连接的客户端映射 (XTcpSocket* -> 接收缓冲区) |
 * | m_observer | XModbusTcpConnectionObserver* | 连接观察器（可选） |
 */
typedef struct XModbusTcpServer {
    XModbusServer m_base;                   ///< 继承自XModbusServer
    XTcpServer* m_tcpServer;                ///< TCP服务器对象
    XMap* m_connectedClients;               ///< 已连接客户端映射 (XTcpSocket* -> XByteArray*)
    XModbusTcpConnectionObserver* m_observer; ///< 连接观察器（可选）
} XModbusTcpServer;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

/**
 * @brief 初始化XModbusTcpServer的虚函数表
 * @return 初始化完成的虚函数表指针
 */
XVtable* XModbusTcpServer_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XModbusTcpServer实例
 * @return 成功返回指向新分配XModbusTcpServer对象的指针，失败返回NULL
 */
XModbusTcpServer* XModbusTcpServer_create_ex(XMemoryType memory);

/**
 * @brief 初始化一个已分配的XModbusTcpServer实例
 * @param server 待初始化的XModbusTcpServer对象指针（非NULL）
 */
void XModbusTcpServer_init(XModbusTcpServer* server);

/******************************************************************************************
 * 连接观察器接口
 ******************************************************************************************/

/**
 * @brief 安装连接观察器
 * @param server XModbusTcpServer实例指针
 * @param observer 连接观察器指针（传入NULL卸载观察器）
 * @note 观察器用于在建立新连接时进行访问控制
 */
void XModbusTcpServer_installConnectionObserver(XModbusTcpServer* server,
    XModbusTcpConnectionObserver* observer);

/******************************************************************************************
 * 继承自父类的API（使用宏定义）
 ******************************************************************************************/

#define XModbusTcpServer_serverAddress            XModbusServer_serverAddress
#define XModbusTcpServer_setServerAddress         XModbusServer_setServerAddress
#define XModbusTcpServer_data1                    XModbusServer_data1
#define XModbusTcpServer_setData1                 XModbusServer_setData1
#define XModbusTcpServer_setData2                 XModbusServer_setData2
#define XModbusTcpServer_data2                    XModbusServer_data2
#define XModbusTcpServer_setMap_base              XModbusServer_setMap_base
#define XModbusTcpServer_setMap_move_base         XModbusServer_setMap_move_base
#define XModbusTcpServer_setMap_ref_base          XModbusServer_setMap_ref_base
#define XModbusTcpServer_value_base               XModbusServer_value_base
#define XModbusTcpServer_value_const_base         XModbusServer_value_const_base
#define XModbusTcpServer_setValue_base            XModbusServer_setValue_base
#define XModbusTcpServer_setValue_move_base       XModbusServer_setValue_move_base
#define XModbusTcpServer_processesBroadcast       XModbusServer_processesBroadcast
#define XModbusTcpServer_processesBroadcast_base  XModbusServer_processesBroadcast_base
#define XModbusTcpServer_dataWritten_signal       XModbusServer_dataWritten_signal

/******************************************************************************************
 * 信号接口
 ******************************************************************************************/

/**
 * @brief 触发modbusClientDisconnected信号
 * @param server XModbusTcpServer实例指针
 * @param modbusClient 断开的客户端socket
 * @return 信号标识
 */
void* XModbusTcpServer_modbusClientDisconnected_signal(XModbusTcpServer* server, XTcpSocket* modbusClient);

/******************************************************************************************
 * 内存管理宏
 ******************************************************************************************/

#define XModbusTcpServer_deinitLater          XModbusServer_deinitLater
#define XModbusTcpServer_deleteLater          XModbusServer_deleteLater

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XModbusTcpServer_create
#define XModbusTcpServer_create(...) XModbusTcpServer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif // XMODBUSTCPSERVER_H
