#ifndef XMODBUSTCPSERVER_H
#define XMODBUSTCPSERVER_H

#include "XModbusServer.h"
#include "XModbusPdu.h"
#include "XTcpServer.h"
#include "XTcpSocket.h"
#include "XTimer.h"

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
    XModbusTcpConnectionObserver* m_observer; ///< 连接观察者（可选）
    /* [死链巡检下沉 2026-10-29] 服务端内置 CLOSE-WAIT/半开连接巡检定时
     * 器（1s；open 启动、close/deinit 停止，随父子链级联析构）。探测复用
     * posix 平台「假 EOF 复核」手法（XDeviceNetwork_posix.c 同款
     * recv(MSG_PEEK|MSG_DONTWAIT)，不消费字节与 IO 环在途读无竞态），
     * Windows 探测通道未内置（原应用层机制即 _WIN32 裁剪），定时器不
     * 启动、sweep 为空操作。 */
    XTimer* m_sweepTimer;                   ///< 死链巡检定时器（内置）
    /* readyRead 读暂存（堆 4KiB）：避免每次收包事件在栈上压 4KiB
     * （嵌入式线程栈小）；init 置 NULL，readyRead 首拍惰性分配，
     * deinit 释放。仅供实现使用。 */
    uint8_t* m_readScratch;                 ///< socket 读取暂存缓冲（堆，复用容量）
} XModbusTcpServer;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

/**
 * @brief 初始化XModbusTcpServer的虚函数表
 * @return 初始化完成的虚函数表指针；不可释放，多次调用返回同一实例
 * @note 本框架为单线程事件循环模型：class_init 无锁保护，
 *       须在事件循环启动前（或同一线程内）调用，通常由 create 接口自动完成
 */
XVtable* XModbusTcpServer_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XModbusTcpServer实例
 * @param memory 内存类型（XCLASS_DEFAULT_MEMORY_TYPE 使用系统默认内存）
 * @return 成功返回指向新分配XModbusTcpServer对象的指针，失败返回NULL
 * @note 返回的对象必须通过 XObject_deleteLater 释放
 */
XModbusTcpServer* XModbusTcpServer_create_ex(XMemoryType memory);

/**
 * @brief 初始化一个已分配的XModbusTcpServer实例
 * @param server 待初始化的XModbusTcpServer对象指针；NULL 时不执行任何操作
 * @return 无。
 * @note 初始化基类并创建内部 XTcpServer、客户端映射表与死链巡检定时器
 *       （1s 周期，open 启动/close 停止）；观察器为空，须另行安装；
 *       与 XClassDeinit/XClassDelete 成对出现
 */
void XModbusTcpServer_init(XModbusTcpServer* server);

/******************************************************************************************
 * 连接观察器接口
 ******************************************************************************************/

/**
 * @brief 安装连接观察器
 * @param server XModbusTcpServer实例指针；NULL 不执行任何操作
 * @param observer 连接观察器指针（借用，服务器不取得所有权，调用方须保证
 *        其生命周期覆盖服务器的使用期；传入NULL卸载观察器）
 * @note 观察器用于在建立新连接时进行访问控制（拒绝即关闭该 socket）
 */
void XModbusTcpServer_installConnectionObserver(XModbusTcpServer* server,
    XModbusTcpConnectionObserver* observer);

/******************************************************************************************
 * 死链巡检（[下沉 2026-10-29] 应用层 mb_slaveSweepDeadClients 机制收编）
 ******************************************************************************************/

/**
 * @brief 立即执行一轮死链（CLOSE-WAIT/半开）巡检回收
 * @param server XModbusTcpServer实例指针
 * @details 对 m_connectedClients 逐条做 recv(MSG_PEEK|MSG_DONTWAIT)
 *          假 EOF 复核（posix；Windows 空操作）：pr>0=有数据待读（活链，
 *          交正常 readyRead 路径）；pr<0 且 EAGAIN/EWOULDBLOCK=空闲活链；
 *          pr==0=对端真 FIN 或其余硬错误（ECONNRESET/ENOTSOCK/EBADF，
 *          含已 close 残留表项）→ 摘映射表、断信号、发
 *          modbusClientDisconnected、close+脱父链+deleteLater（同
 *          onClientDisconnected 语义）。探测不消费字节，与 IO 环在途读
 *          无竞态；表空零开销。
 * @note 服务端已内置 1s 周期定时器（open 启动/close 停止）自动巡检，
 *       本 API 供调用方按需手动触发或自管巡检节奏。
 */
void XModbusTcpServer_sweepDeadClients(XModbusTcpServer* server);

/**
 * @brief 构建 Modbus TCP 响应帧（MBAP 头 + PDU）到调用方缓冲区。
 * @details [异常位根修 2026-10-30] 线上功能码取 functionCodeRaw：Modbus
 *          规范要求异常应答的功能码为 FC|0x80，而 XModbusPdu_functionCode()
 *          按设计屏蔽异常位（返回原始 FC）——用后者拼帧，主站会把异常应答
 *          当成正常应答（异常码被当数据解析且不报异常错误）。RTU 路径走
 *          XModbusAdu 编码、用的是 Raw，一直是对的。
 * @param buf 输出缓冲区（至少 8 字节）
 * @param bufSize 缓冲区容量（不足则不写）
 * @param transactionId 请求事务号（原样回填）
 * @param unitId 从站地址（原样回填）
 * @param response 应答 PDU（普通应答或异常应答）
 * @return 帧总长（字节）；参数非法或缓冲区不足返回 0
 */
size_t XModbusTcpServer_buildResponseFrame(uint8_t* buf, size_t bufSize,
    uint16_t transactionId, uint8_t unitId, const XModbusResponse* response);

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
/* TCP 从站不处理广播（基类默认实现恒 false；Modbus TCP 无广播语义）。 */
#define XModbusTcpServer_processesBroadcast       XModbusServer_processesBroadcast
#define XModbusTcpServer_processesBroadcast_base  XModbusServer_processesBroadcast_base
#define XModbusTcpServer_dataWritten_signal       XModbusServer_dataWritten_signal
#define XModbusTcpServer_dataRead_signal          XModbusServer_dataRead_signal

/******************************************************************************************
 * 信号接口
 ******************************************************************************************/

/**
 * @brief 触发modbusClientDisconnected信号（客户端断开/被死链巡检回收时发射）
 * @param server XModbusTcpServer实例指针
 * @param modbusClient 断开的客户端socket（借用；发射后服务器即将回收该
 *        socket，订阅方不得长期持有）
 * @return 未定义——当前实现发射信号后不返回有效值，调用方不得使用返回值。
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
#define XModbusTcpServer_create() XModbusTcpServer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XMODBUSTCPSERVER_H
