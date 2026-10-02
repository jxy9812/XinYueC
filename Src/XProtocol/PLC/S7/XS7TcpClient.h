#ifndef XS7TCPCLIENT_H
#define XS7TCPCLIENT_H

#include "XPlc_config.h"
#include "XPlcClient.h"
#include "XS7Types.h"
#include "XS7Session.h"
#include "XHashMap.h"
#include "XQueue.h"
#include "XByteArray.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON

/**
 * @file XS7TcpClient.h
 * @brief 西门子 S7（ISO-on-TCP）客户端，三层之 TcpClient
 * @details 继承链：XObject → XPlcDevice[Open,Close] → XPlcClient[SendRequest,
 *          ProcessResponse] → XS7TcpClient（重载 Open/Close/Deinit/TimerEvent/
 *          SendRequest/ProcessResponse）。传输层完全复用库内 XNetwork
 *          （XTcpSocket → XAbstractSocket → XIODevice），禁止直接调用
 *          socket/Winsock/lwIP API。
 *
 * @par 关键约束
 * - AmQ=1 串行发送：S7-1200/1500 单 IP 连接数有限，m_sendQueue 单 in-flight
 *   + 队列，一个完成才发下一个（不能照搬 XModbus 多 pending 并发）
 * - 分片读写：单 item 按字节切片（读 ≤pduLen-18，写 ≤pduLen-35），
 *   每片独立 pending/超时/重试，全部完成后拼接解码；多 item 批量留后续
 * - 唯一触碰字节流的组件是 XS7Session_feed（粘包/半包/坏包）
 *
 * @par S7-1200/1500 联调须知（@details，否则会出现"连上读不到"）
 * - TIA 中关闭目标 DB 的"优化的块访问"（Optimized block access）
 * - 勾选"允许来自远程伙伴的 PUT/GET 通信"，防护等级需放开
 *
 * @par 使用示例
 * @code
 * XS7TcpClient* client = XS7TcpClient_create();
 * XPlcDevice_setConnectionParameter_ref((XPlcDevice*)client,
 *     XPlcDevice_NetworkAddressParameter, XVariant_create_utf8_str("192.168.0.1"));
 * XPlcDevice_setConnectionParameter_ref((XPlcDevice*)client,
 *     XPlcDevice_RackParameter, XVariant_create_int(0));
 * XPlcDevice_setConnectionParameter_ref((XPlcDevice*)client,
 *     XPlcDevice_SlotParameter, XVariant_create_int(1));
 * XPlcDevice_connectDevice((XPlcDevice*)client);
 * // stateChanged → Handshaking → Connected 后：
 * XPlcReply* reply = XS7TcpClient_sendRead_2(client, "DB1.DBW10", XS7Value_Word, 1);
 * XObject_setParent(reply, client);
 * XObject_connect_1(reply, XSignal(XPlcReply_finished_signal), reply,
 *     XObject_deleteLater, XConnectionType_Auto);
 * @endcode
 */

XCLASS_DEFINE_BEGING(XS7TcpClient)
XCLASS_DEFINE_EXTEND_END(XS7TcpClient, XPlcClient)

 /**
  * @brief S7 TCP 客户端结构体
  * @details 继承自XPlcClient
  *
  * @par 成员说明
  * | 成员 | 类型 | 说明 |
  * |------|------|------|
  * | m_base | XPlcClient | 继承自XPlcClient基类 |
  * | m_session | XS7Session* | 握手状态机+分帧器（对象拥有） |
  * | m_pendingRequests | XHashMap* | uint16 pduRef → XS7PendingRequest |
  * | m_timerMap | XHashMap* | XTimerId → uint16 pduRef（O(1) 超时定位） |
  * | m_sendQueue | XQueue* | AmQ=1 串行发送队列（XS7PendingRequest*） |
 * | m_requestData | XByteArray* | 复用组帧缓冲 |
 * | m_negotiatedPduLen | uint16_t | 协商结果，默认兜底 240 |
 * | m_pumping | bool | 发送队列驱动中标志（防 setError→槽→再发送的重入破坏队列） |
 * | m_clearingAll | bool | 清理在途请求中标志（防 setError→槽→断开→清理的重入二次释放） |
 */
typedef struct XS7TcpClient {
    XPlcClient   m_base;               ///< 继承自XPlcClient基类
    XS7Session*  m_session;            ///< 握手状态机+分帧器（对象拥有）
    XHashMap*    m_pendingRequests;    ///< 待响应请求映射表 (uint16_t -> XS7PendingRequest)
    XHashMap*    m_timerMap;           ///< 定时器反向映射 (XTimerId -> uint16_t)
    XQueue*      m_sendQueue;          ///< AmQ=1 串行发送队列
    XByteArray*  m_requestData;        ///< 复用组帧缓冲
    uint16_t     m_negotiatedPduLen;   ///< 协商后的 PDU 长度（默认兜底 XS7_FALLBACK_PDU_LEN）
    bool         m_pumping;            ///< pumpSendQueue 迭代中标志（重入时让位给外层循环）
    bool         m_clearingAll;        ///< clearAllPendingRequests 执行中标志（重入直接返回）
} XS7TcpClient;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

 /**
  * @brief 初始化XS7TcpClient的虚函数表
  * @return 初始化完成的虚函数表指针
  * @note 该函数是线程安全的，多次调用返回同一虚表实例；
  *       先 XVTABLE_INHERIT_XCLASS(XPlcClient) 后 XVTABLE_OVERLOAD_DEFAULT，顺序不可颠倒
  */
XVtable* XS7TcpClient_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XS7TcpClient实例
 * @param memory 内存分配类型
 * @return 成功返回新实例指针，失败返回NULL
 * @note 返回的对象必须通过 XObject_deleteLater 释放；
 *       创建时初始化 pending/timer/sendQueue/组帧缓冲
 */
XS7TcpClient* XS7TcpClient_create_ex(XMemoryType memory);

/**
 * @brief 初始化一个已分配的XS7TcpClient实例
 * @param client 待初始化的客户端指针（非NULL）
 * @note 调用 XPlcClient_init，创建内部容器，协商长度置兜底 240
 */
void XS7TcpClient_init(XS7TcpClient* client);

/******************************************************************************************
 * 读写 API（首期范围）
 ******************************************************************************************/

 /**
  * @brief 发送读请求
  * @param client 客户端指针（非NULL）
  * @param address 地址字符串（如 "DB1.DBW10"，主版本 XString）
  * @param type 值类型
  * @param count 元素个数（String 时为容量 n）
  * @return 异步回复对象（调用者持有，建议 setParent(client) + finished→deleteLater），
  *         失败返回NULL
  */
XPlcReply* XS7TcpClient_sendRead(XS7TcpClient* client, const XString* address, XS7ValueType type, int count);

/**
 * @brief 发送读请求（UTF-8 转发版本）
 * @param client 客户端指针（非NULL）
 * @param addressUtf8 UTF-8 地址字符串
 * @param type 值类型
 * @param count 元素个数
 * @return 异步回复对象，失败返回NULL
 */
XPlcReply* XS7TcpClient_sendRead_2(XS7TcpClient* client, const char* addressUtf8, XS7ValueType type, int count);

/**
 * @brief 发送写请求
 * @param client 客户端指针（非NULL）
 * @param address 地址字符串（主版本 XString）
 * @param type 值类型
 * @param count 元素个数（String 时为容量 n）
 * @param value 写入值（非NULL）
 * @return 异步回复对象，失败返回NULL
 */
XPlcReply* XS7TcpClient_sendWrite(XS7TcpClient* client, const XString* address, XS7ValueType type,
                                  int count, const XVariant* value);

/**
 * @brief 发送写请求（UTF-8 转发版本）
 * @param client 客户端指针（非NULL）
 * @param addressUtf8 UTF-8 地址字符串
 * @param type 值类型
 * @param count 元素个数
 * @param value 写入值（非NULL）
 * @return 异步回复对象，失败返回NULL
 */
XPlcReply* XS7TcpClient_sendWrite_2(XS7TcpClient* client, const char* addressUtf8, XS7ValueType type,
                                     int count, const XVariant* value);

/**
 * @brief 发送原始字节块读请求
 * @param client 客户端指针（非NULL）
 * @param address 地址字符串（主版本 XString）
 * @param byteCount 读取字节数
 * @return 异步回复对象（rawResult 为原始数据块），失败返回NULL
 */
XPlcReply* XS7TcpClient_sendReadRaw(XS7TcpClient* client, const XString* address, int byteCount);

/******************************************************************************************
 * 运维 API（XS7_CONTROL_ON 门控：声明与实现、菜单项三处同步 #if）
 ******************************************************************************************/

#if XS7_CONTROL_ON
 /**
  * @brief 发送 PLC Run 启动请求
  * @param client 客户端指针（非NULL）
  * @param mode 启动模式（Hot 热启动 / Cold 冷启动）
  * @return 异步回复对象，失败返回NULL
  * @warning 危险操作：对运行中生产设备执行会改变 PLC 运行状态
  * @note 应答 0x02（已运行）/0x07（已停止）视为成功，写入 errorString 提示
  */
XPlcReply* XS7TcpClient_sendPlcRun(XS7TcpClient* client, XS7RunMode mode);

/**
 * @brief 发送 PLC Stop 停止请求
 * @param client 客户端指针（非NULL）
 * @return 异步回复对象，失败返回NULL
 * @warning 危险操作：会停止 PLC 程序执行
 */
XPlcReply* XS7TcpClient_sendPlcStop(XS7TcpClient* client);
#endif /* XS7_CONTROL_ON */

/******************************************************************************************
 * 块传输 API（XS7_BLOCK_ON 门控：声明与实现、菜单项三处同步 #if）
 ******************************************************************************************/

#if XS7_BLOCK_ON
 /**
  * @brief 请求块列表
  * @param client 客户端指针（非NULL）
  * @return 异步回复对象（完成后 result 为 XVector<XS7BlockInfo>），失败返回NULL
  */
XPlcReply* XS7TcpClient_sendListBlocks(XS7TcpClient* client);

/**
 * @brief 上传（读取）一个块
 * @param client 客户端指针（非NULL）
 * @param blockName 块名（如 "OB1"/"DB1"，主版本 XString）
 * @return 异步回复对象（完成后 rawResult 为块字节），失败返回NULL
 */
XPlcReply* XS7TcpClient_sendUploadBlock(XS7TcpClient* client, const XString* blockName);

/**
 * @brief 下载（写入）一个块
 * @param client 客户端指针（非NULL）
 * @param blockName 块名（主版本 XString）
 * @param payload 块字节载荷（非NULL）
 * @return 异步回复对象，失败返回NULL
 */
XPlcReply* XS7TcpClient_sendDownloadBlock(XS7TcpClient* client, const XString* blockName,
                                          const XByteArray* payload);
#endif /* XS7_BLOCK_ON */

/******************************************************************************************
 * 继承自父类的API（使用宏定义转发，不写重复包装函数）
 ******************************************************************************************/

 /** @brief 连接设备（异步发起） */
#define XS7TcpClient_connectDevice          XPlcDevice_connectDevice
 /** @brief 断开设备连接 */
#define XS7TcpClient_disconnectDevice       XPlcDevice_disconnectDevice
 /** @brief 获取连接参数（深拷贝） */
#define XS7TcpClient_connectionParameter    XPlcDevice_connectionParameter
 /** @brief 获取连接参数（常量引用） */
#define XS7TcpClient_connectionParameter_const XPlcDevice_connectionParameter_const
 /** @brief 设置连接参数（深拷贝） */
#define XS7TcpClient_setConnectionParameter XPlcDevice_setConnectionParameter
 /** @brief 获取设备状态 */
#define XS7TcpClient_state                  XPlcDevice_state
 /** @brief 获取设备错误码 */
#define XS7TcpClient_error                  XPlcDevice_error
 /** @brief 获取设备错误描述（深拷贝） */
#define XS7TcpClient_errorString            XPlcDevice_errorString
 /** @brief 获取底层IO设备 */
#define XS7TcpClient_device                 XPlcDevice_device
 /** @brief 获取请求超时时间（毫秒） */
#define XS7TcpClient_timeout                XPlcClient_timeout
 /** @brief 设置请求超时时间（毫秒） */
#define XS7TcpClient_setTimeout             XPlcClient_setTimeout
 /** @brief 获取请求重试次数 */
#define XS7TcpClient_numberOfRetries        XPlcClient_numberOfRetries
 /** @brief 设置请求重试次数 */
#define XS7TcpClient_setNumberOfRetries     XPlcClient_setNumberOfRetries
 /** @brief 是否启用自动重连 */
#define XS7TcpClient_autoReconnect          XPlcClient_autoReconnect
 /** @brief 设置是否启用自动重连 */
#define XS7TcpClient_setAutoReconnect       XPlcClient_setAutoReconnect
 /** @brief 获取重连间隔（毫秒） */
#define XS7TcpClient_reconnectInterval      XPlcClient_reconnectInterval
 /** @brief 设置重连间隔（毫秒） */
#define XS7TcpClient_setReconnectInterval   XPlcClient_setReconnectInterval
 /** @brief 获取最大重连次数（-1无限） */
#define XS7TcpClient_maxReconnectAttempts   XPlcClient_maxReconnectAttempts
 /** @brief 设置最大重连次数（-1无限） */
#define XS7TcpClient_setMaxReconnectAttempts XPlcClient_setMaxReconnectAttempts

/******************************************************************************************
 * 内存管理宏
 ******************************************************************************************/

 /** @brief 析构函数（延迟删除） */
#define XS7TcpClient_deleteLater            XPlcClient_deleteLater
 /** @brief 析构函数（延迟反初始化） */
#define XS7TcpClient_deinitLater            XPlcClient_deinitLater

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XS7TcpClient_create
#define XS7TcpClient_create() XS7TcpClient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XS7TCPCLIENT_H
