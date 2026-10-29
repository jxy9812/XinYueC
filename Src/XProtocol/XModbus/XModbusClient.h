/**
 * @file       XModbusClient.h
 * @brief      Modbus 客户端核心公开 API。
 * @details    对齐 Qt QModbusClient；抽象基类只负责请求构建、超时和自动重连策略，
 *             RTU/TCP 等具体传输由子类实现，不直接调用平台串口或套接字 API。
 */

#ifndef XMODBUSCLIENT_H
#define XMODBUSCLIENT_H
#include "XModbus_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XMODBUS_ON
#if XMODBUS_CLIENT_ON

#include <stdint.h>
#include <stdbool.h>
#include "XModbusDevice.h"
#include "XModbusReply.h"
#include "XModbusDataUnit.h"
#include "XVector.h"
#include "XModbusPdu.h"

XCLASS_DEFINE_BEGING(XModbusClient)
XCLASS_DEFINE_ENUM(XModbusClient, ProcessResponse) = XCLASS_VTABLE_GET_SIZE(XModbusDevice),
XCLASS_DEFINE_ENUM(XModbusClient, ProcessPrivateResponse),
XCLASS_DEFINE_ENUM(XModbusClient, SendRawRequest),    ///< 发送原始请求（虚函数，子类重写）
XCLASS_DEFINE_END(XModbusClient)
/**
 * @brief Modbus客户端核心结构体（继承自XModbusDevice）
 * @details 封装了Modbus主站/客户端的核心功能，用于向从站发送读写请求。
 *          这是一个抽象基类，具体的通信后端（如RTU/TCP）需由子类实现。
 */
/* [单事务模式 2026-10-15] 单事务发送等待队列元素：等待空闲槽上线的
 * 请求（串行开关置位且在途>0 时入队，前序完成后按序发出）。队列仅在
 * 单事务模式开启期间由 XModbusClient_setSerialTransactions 创建，
 * 关闭模式即销毁释放内存。 */
struct XModbusSerialSend {
    struct XModbusRequest* request; /**< 请求（所有权入队转移，随 Reply 释放）；
                                     * 允许为 NULL——表示请求由 Reply 持有
                                     * （Ref 语义入队时用），出队方从 Reply 取。 */
    struct XModbusReply* reply;     /**< 关联 Reply（完成时向调用方通告）。 */
    int serverAddress;              /**< 从机地址。 */
    XModbusRequestPriority priority; /**< 请求优先级（高优先队列先出队）。 */
};

/** @brief 发送等待队列容量上限（元素数；创建时预留此容量，超出则入队失败）。 */
#define XMODBUS_SERIAL_QUEUE_CAPACITY 64

typedef struct XModbusClient {
    XModbusDevice m_base;           ///< 继承自XModbusDevice基类
    size_t m_timeout;               ///< 请求超时时间（毫秒）
    XTimerId m_timeoutTimer;        ///< 超时定时器ID
    XHashMap* m_poolMap;            /**< 轮询映射表 <XTimerId,XModbusReply*>；仅供实现使用，由对象拥有并在析构时释放；NULL 表示尚未创建。 */
    // 自动重连配置
    bool m_autoReconnect;               ///< 是否启用自动重连
    int16_t m_numberOfRetries;          ///< 请求超时重试次数
    int16_t m_reconnectAttempts;        ///< 当前重连尝试次数
    int16_t m_maxReconnectAttempts;     ///< 最大重连次数（-1表示无限）
    size_t m_reconnectInterval;        ///< 重连间隔（毫秒）
    XTimerId m_reconnectTimer;      ///< 重连定时器ID
    /* [单事务模式 2026-10-15] 串行发送开关（对标 Qt Modbus 单连接单事务
     * 从机场景）：置位后，上一请求在途时新请求进入传输层发送队列排队，
     * 前序完成后按 FIFO 发出——单事务从机（远程 IO 等）忙时丢弃交叠
     * 请求，pipelining 并发会丢写帧。默认关=pipelining 多事务（规范
     * 允许，多事务从机零回归）。 */
    bool m_serialTransactions;      /**< 单事务串行发送开关（默认 false）。 */
    /* 发送等待队列：XVector + 按优先级有序插入（对齐 Qt QEventQueue 语义——
     * QList 按 priority 有序插入，值越大越靠前出队，同优先级保持 FIFO）。
     * 创建时预留默认容量，避免频繁重分配。 */
    XVector* m_serialQueue;         /**< 发送等待队列（XVector，元素按优先级有序）。 */
} XModbusClient;


/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

/**
 * @brief 初始化XModbusClient的虚函数表
 * @return 指向初始化完成的XVtable的指针
 * @note 虚表在首次调用时初始化并缓存，此后返回同一实例。
 *       本框架为单线程事件循环模型：class_init 无锁保护，
 *       须在事件循环启动前（或同一线程内）调用，通常由 create 接口自动完成。
 */
XVtable* XModbusClient_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XModbusClient实例
 * @param memory 内存类型（XCLASS_DEFAULT_MEMORY_TYPE 使用系统默认内存）
 * @return 成功返回指向新分配XModbusClient对象的指针，失败返回NULL
 * @note 返回的对象必须通过 XObject_deleteLater 或 XClassDelete 释放
 */
XModbusClient* XModbusClient_create_ex(XMemoryType memory);

/**
 * @brief 初始化一个已分配的XModbusClient实例
 * @param client 待初始化的XModbusClient对象指针（非NULL）
 * @note 该函数会初始化基类成员、超时和重试次数等默认值
 */
void XModbusClient_init(XModbusClient* client);

/******************************************************************************************
 * Public API (严格对齐 QModbusClient)
 ******************************************************************************************/

 /**
 * @brief 发送Modbus读取请求
 * @param client 客户端实例指针（非NULL）
 * @param read 要读取的数据单元（包含寄存器类型、起始地址、数量）
 * @param serverAddress 目标从站地址
 * @return 成功返回指向XModbusReply对象的指针，失败返回NULL
 * @note 调用者拥有返回的XModbusReply对象，需负责其生命周期管理
 */
XModbusReply* XModbusClient_sendReadRequest(XModbusClient* client, const XModbusDataUnit* read,
                                          int serverAddress, XModbusRequestPriority priority);
/**
* @brief 轮询方式发送Modbus读取请求并等待响应（阻塞直到成功、超时或被取消）
* @param client 客户端实例指针（非NULL）
* @param read 要读取的数据单元（包含寄存器类型、起始地址、数量）
* @param serverAddress 目标从站地址
* @param pollIntervalMs 轮询间隔（毫秒），每隔此时间检查一次接收缓冲
* @return 成功返回指向XModbusReply对象的指针（调用者负责释放），失败返回NULL
* @note 可用 XModbusClient_cancelPoll 取消，被取消的轮询返回 NULL
*/
XModbusReply* XModbusClient_pollReadRequest(XModbusClient* client, const XModbusDataUnit* read, int serverAddress, int pollIntervalMs);
/**
* @brief 发送Modbus写入请求
* @param client 客户端实例指针（非NULL）
* @param write 要写入的数据单元（包含寄存器类型、起始地址、值列表）
* @param serverAddress 目标从站地址
* @return 成功返回指向XModbusReply对象的指针，失败返回NULL
* @note 调用者拥有返回的XModbusReply对象，需负责其生命周期管理
*/
XModbusReply* XModbusClient_sendWriteRequest(XModbusClient* client, const XModbusDataUnit* write,
                                           int serverAddress, XModbusRequestPriority priority);
/**
* @brief 轮询方式发送Modbus写入请求并等待响应（阻塞直到成功、超时或被取消）
* @param client 客户端实例指针（非NULL）
* @param write 要写入的数据单元（包含寄存器类型、起始地址、值列表）
* @param serverAddress 目标从站地址
* @param pollIntervalMs 轮询间隔（毫秒），每隔此时间检查一次接收缓冲
* @return 成功返回指向XModbusReply对象的指针（调用者负责释放），失败返回NULL
* @note 可用 XModbusClient_cancelPoll 取消，被取消的轮询返回 NULL
*/
XModbusReply* XModbusClient_pollWriteRequest(XModbusClient* client, const XModbusDataUnit* write, int serverAddress, int pollIntervalMs);
/**
* @brief 发送Modbus读写组合请求（仅支持0x17功能码）
* @param client 客户端实例指针（非NULL）
* @param read 读取部分的数据单元
* @param write 写入部分的数据单元
* @param serverAddress 目标从站地址
* @return 成功返回指向XModbusReply对象的指针，失败返回NULL
* @note 调用者拥有返回的XModbusReply对象，需负责其生命周期管理
*/
XModbusReply* XModbusClient_sendReadWriteRequest(XModbusClient* client, const XModbusDataUnit* read, const XModbusDataUnit* write, int serverAddress);
/**
* @brief 轮询方式发送Modbus读写组合请求并等待响应（仅支持0x17功能码，阻塞直到成功、超时或被取消）
* @param client 客户端实例指针（非NULL）
* @param read 读取部分的数据单元
* @param write 写入部分的数据单元
* @param serverAddress 目标从站地址
* @param pollIntervalMs 轮询间隔（毫秒），每隔此时间检查一次接收缓冲
* @return 成功返回指向XModbusReply对象的指针（调用者负责释放），失败返回NULL
* @note 可用 XModbusClient_cancelPoll 取消，被取消的轮询返回 NULL
*/
XModbusReply* XModbusClient_pollReadWriteRequest(XModbusClient* client, const XModbusDataUnit* read, const XModbusDataUnit* write, int serverAddress,int pollIntervalMs);
/**
* @brief 发送原始Modbus请求PDU（虚函数，通过虚函数表调用）
* @param client 客户端实例指针（非NULL）
* @param request 原始请求PDU
* @param serverAddress 目标从站地址
* @return 成功返回指向XModbusReply对象的指针，失败返回NULL
* @note 调用者拥有返回的XModbusReply对象，需负责其生命周期管理
* @note 子类必须重写此虚函数，实现协议特定的帧构建和发送逻辑
*/
XModbusReply* XModbusClient_sendRawRequest_base(XModbusClient* client, const XModbusRequest* request, int serverAddress);
/**
* @brief 发送原始Modbus请求PDU（移动语义虚函数，通过虚函数表调用）
* @param client 客户端实例指针（非NULL）
* @param request 原始请求PDU（移动：成功后其资源被转移到Reply内部，源对象变为空壳，仍由调用者负责释放）
* @param serverAddress 目标从站地址
* @return 成功返回指向XModbusReply对象的指针，失败返回NULL（request未被接管，仍归调用者）
* @note 子类必须重写此虚函数，实现协议特定的帧构建和发送逻辑
*/
XModbusReply* XModbusClient_sendRawRequest_move_base(XModbusClient* client, const XModbusRequest* request, int serverAddress);
/**
* @brief 发送原始Modbus请求PDU（引用语义虚函数，通过虚函数表调用）
* @param client 客户端实例指针（非NULL）
* @param request 原始请求PDU（引用：成功后由返回的Reply接管并负责释放；失败时未被接管，仍归调用者）
* @param serverAddress 目标从站地址
* @return 成功返回指向XModbusReply对象的指针，失败返回NULL（request未被接管，仍归调用者）
* @note 子类必须重写此虚函数，实现协议特定的帧构建和发送逻辑
*/
XModbusReply* XModbusClient_sendRawRequest_ref_base(XModbusClient* client, const XModbusRequest* request, int serverAddress);
/**
 * @brief 轮询方式发送Modbus请求并等待响应（阻塞直到成功或超时）
 * @param client        客户端实例指针（非NULL）
 * @param request       原始请求PDU
 * @param serverAddress 目标从站地址
 * @param pollIntervalMs 轮询间隔（毫秒），每隔此时间检查一次接收缓冲
 * @return 成功返回指向XModbusReply对象的指针（调用者负责释放），失败返回NULL
 */
XModbusReply* XModbusClient_pollRawRequest(XModbusClient* client,
    const XModbusRequest* request,
    int serverAddress,
    int pollIntervalMs);
/**
* @brief 轮询方式发送Modbus请求（移动语义：内部将request的资源转移到新请求对象后轮询）
* @param client        客户端实例指针（非NULL）
* @param request       原始请求PDU（移动：源对象变为空壳，仍由调用者负责释放）
* @param serverAddress 目标从站地址
* @param pollIntervalMs 轮询间隔（毫秒），每隔此时间检查一次接收缓冲
* @return 成功返回指向XModbusReply对象的指针（调用者负责释放），失败返回NULL
* @note 可用 XModbusClient_cancelPoll 取消，被取消的轮询返回 NULL
*/
XModbusReply* XModbusClient_pollRawRequest_move(XModbusClient* client,
    const XModbusRequest* request,
    int serverAddress,
    int pollIntervalMs);
/**
* @brief 轮询方式发送Modbus请求（引用语义）
* @param client        客户端实例指针（非NULL）
* @param request       原始请求PDU（引用：成功后由返回的Reply接管并负责释放；失败时未被接管，仍归调用者，需用XClassDelete释放）
* @param serverAddress 目标从站地址
* @param pollIntervalMs 轮询间隔（毫秒），每隔此时间检查一次接收缓冲
* @return 成功返回指向XModbusReply对象的指针（调用者负责释放），失败返回NULL
* @note 可用 XModbusClient_cancelPoll 取消，被取消的轮询返回 NULL
*/
XModbusReply* XModbusClient_pollRawRequest_ref(XModbusClient* client,
    XModbusRequest* request,
    int serverAddress,
    int pollIntervalMs);
/**
 * @brief 取消指定客户端的当前轮询请求（如果有正在进行的轮询）
 * @param client 客户端实例指针
 * @return 成功找到并取消该Reply的轮询返回true；参数无效或该Reply不在轮询中返回false
 * @note 非线程安全：内部直接遍历轮询池并 killTimer，只能在拥有该客户端
 *       事件循环的同一线程内调用（不要在另一线程或信号处理函数中调用）
 * @note 被取消的轮询函数将返回 NULL，且不会产生 XModbusReply 对象
 */
bool XModbusClient_cancelPoll(XModbusClient* client, XModbusReply*reply);

/**
* @brief 获取当前请求超时时间
* @param client 客户端实例指针（非NULL）
* @return 超时时间（毫秒）
*/
size_t XModbusClient_timeout(const XModbusClient* client);

/**
* @brief 设置请求超时时间
* @param client 客户端实例指针（非NULL）
* @param newTimeout 新的超时时间（毫秒）
* @note 设置后会触发 timeoutChanged 信号
*/
void XModbusClient_setTimeout(XModbusClient* client, size_t newTimeout);

/**
* @brief 获取当前请求重试次数
* @param client 客户端实例指针（非NULL）
* @return 重试次数
*/
int16_t XModbusClient_numberOfRetries(const XModbusClient* client);

/** @brief 设置单事务串行发送开关（传输层在途>0 时新请求排队等空闲槽）。
 *  @note 须在连接建立前设置；默认关闭（pipelining）。 */
void XModbusClient_setSerialTransactions(XModbusClient* client, bool enable);
/** @brief 查询单事务串行发送开关。 */
bool XModbusClient_serialTransactions(const XModbusClient* client);

/** @brief 单事务发送队列入队（request 所有权入队转移；成功后随 Reply
 *  生命周期释放）。队列仅在单事务模式开启期间由
 *  XModbusClient_setSerialTransactions 创建/销毁。 */
bool XModbusClient_serialQueuePush(XModbusClient* client,
    struct XModbusRequest* ownedRequest, struct XModbusReply* reply,
    int serverAddress, XModbusRequestPriority priority);
/** @brief 单事务发送队列出队：取队头请求（调用方负责上线/释放）。 */
bool XModbusClient_serialQueuePop(XModbusClient* client,
    struct XModbusRequest** outRequest, struct XModbusReply** outReply,
    int* outServerAddress);
/** @brief 单事务发送队列元素数。 */
int XModbusClient_serialQueueCount(const XModbusClient* client);
/** @brief 清空单事务发送队列：排队 Reply 以 error 完成告知调用方，
 *  排队 Request 随即释放。 */
void XModbusClient_serialQueueClear(XModbusClient* client,
    XModbusDevice_Error error, const char* errorMsg);

/**
* @brief 设置请求重试次数
* @param client 客户端实例指针（非NULL）
* @param number 新的重试次数
*/
void XModbusClient_setNumberOfRetries(XModbusClient* client, uint8_t number);

/******************************************************************************************
 * 自动重连配置 API
 ******************************************************************************************/

/**
* @brief 检查是否启用自动重连
* @param client 客户端实例指针（非NULL）
* @return true表示启用自动重连
*/
bool XModbusClient_autoReconnect(const XModbusClient* client);

/**
* @brief 设置是否启用自动重连
* @param client 客户端实例指针（非NULL）
* @param enabled 是否启用
*/
void XModbusClient_setAutoReconnect(XModbusClient* client, bool enabled);

/**
* @brief 获取重连间隔
* @param client 客户端实例指针（非NULL）
* @return 重连间隔（毫秒）
*/
size_t XModbusClient_reconnectInterval(const XModbusClient* client);

/**
* @brief 设置重连间隔
* @param client 客户端实例指针（非NULL）
* @param interval 重连间隔（毫秒）
*/
void XModbusClient_setReconnectInterval(XModbusClient* client, size_t interval);

/**
* @brief 获取最大重连次数
* @param client 客户端实例指针（非NULL）
* @return 最大重连次数（-1表示无限）
*/
int16_t XModbusClient_maxReconnectAttempts(const XModbusClient* client);

/**
* @brief 设置最大重连次数
* @param client 客户端实例指针（非NULL）
* @param attempts 最大重连次数（-1表示无限）
*/
void XModbusClient_setMaxReconnectAttempts(XModbusClient* client, int16_t attempts);

/**
* @brief 获取当前重连尝试次数
* @param client 客户端实例指针（非NULL）
* @return 当前重连尝试次数
*/
int16_t XModbusClient_reconnectAttempts(const XModbusClient* client);

/******************************************************************************************
 * 信号接口
 ******************************************************************************************/

 /**
 * @brief 触发 timeoutChanged 信号
 * @param client 客户端实例指针（非NULL）
 * @param newTimeout 新的超时值
 * @return 信号标识
 */
void* XModbusClient_timeoutChanged_signal(XModbusClient* client, int newTimeout);
#define XModbusClient_deleteLater		XObject_deleteLater
#define XModbusClient_deinitLater		XObject_deinitLater

#endif /* XMODBUS_CLIENT_ON */
#endif /* XMODBUS_ON */

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XModbusClient_create
#define XModbusClient_create() XModbusClient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XMODBUSCLIENT_H