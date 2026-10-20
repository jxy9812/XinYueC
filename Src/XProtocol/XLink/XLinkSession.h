/** @file       XLinkSession.h
 * @brief      XLink 私有协议会话对象公开 API（收发/分发/订阅/待应答/心跳）。
 * @details    对齐 Qt QTcpSocket + QMqttClient 的组合角色：对上表现为一条可读写
 *             的字节流，对下管理帧边界、消息分发、请求应答关联与链路心跳。
 *             实现只依赖 XinYueC 抽象层（XIODevice/XMemory/XDateTime），
 *             禁止调用 Win32、POSIX 或其他平台 API。
 *
 *             两端对称：同一套编解码、同一张分发表、同一份待应答表，
 *             "服务器端"与"客户端端"的唯一差别是建立链路时谁 accept、谁 connect
 *             ——传输建立之后协议层不再区分身份。因此本类既可驱动控制进程
 *             （被连接方），也可驱动 GUI 进程（主动连接方）。
 *
 *             线程模型：单线程主循环口径，与 XPlatformFbInput_posix 的 fbdev
 *             显示/输入驱动一致——所有 API 均不可阻塞，须在持有者的事件循环
 *             （每圈 poll 回调或 XSocketNotifier）内调用，全类不加锁。
 *
 *             时间口径：所有超时/心跳时长由调用方以 XDateTime_currentMSecsSinceEpoch()
 *             （单调毫秒）传入，本类不自行取时（见「外部依赖约束」时间源规则）。
 */
#ifndef XLINKSESSION_H
#define XLINKSESSION_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置与本协议配置，确保 XLINK_ON / XLINK_SESSION_ON 已定义
   （必须早于下方条件编译判定，否则直接 include 本头文件时开关未定义、
   整段声明会被跳过） */
#include "XLink_config.h"

#if XLINK_ON && XLINK_SESSION_ON

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "XLink_config.h"
#include "XLinkProto.h"
#include "XClass.h"
#include "XIODevice.h"

/* ========================================================================== */
/*                        虚函数表                                           */
/* ========================================================================== */

XCLASS_DEFINE_BEGING(XLinkSession)
/* 新增槽位从基类槽位总数开始编号，否则与 XClass 的槽位撞号（建表容量不足）。 */
XCLASS_DEFINE_ENUM(XLinkSession, dispatch) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_END(XLinkSession)

/* ========================================================================== */
/*                        类型定义                                           */
/* ========================================================================== */

/* 会话对象前置声明：下方分发回调 typedef 的参数需先指名 XLinkSession。 */
typedef struct XLinkSession XLinkSession;

/**
 * @brief 链路状态。
 * @details 状态迁移：Inactive --(setDevice/挂载传输)--> Connected --(对端 Hello
 *          或超时判线)--> Ready <--> Connected；任一状态下检测到传输断开、
 *          协议违例或显式 reset 均回到 Inactive。
 */
typedef enum XLinkSessionState {
    XLinkSessionState_Inactive  = 0, /**< 未挂载传输，或传输已断开/判线（初始态）。 */
    XLinkSessionState_Connected = 1, /**< 已挂载传输但握手未完成，暂不投递业务消息。 */
    XLinkSessionState_Ready     = 2  /**< 握手完成且链路健康，全部分类消息正常投递。 */
} XLinkSessionState;

/**
 * @brief 消息分发回调：由 XLinkSession_dispatch_base 在 pump 中逐帧调用。
 * @param self 会话对象（借用）。
 * @param frame 帧头（借用，仅本次调用有效，不得保存指针）。
 * @param payload 载荷首地址（借用，仅本次调用有效；frame.m_payloadLen 为 0 时为 NULL）。
 * @return 无。
 * @note 回调在调用者的 pump 栈上同步执行，不得反调本对象的阻塞接口。
 */
typedef void (*XLinkSessionDispatchCallback)(XLinkSession* self,
                                             const XLinkFrameHeader* frame,
                                             const uint8_t* payload);

/**
 * @brief 待应答表项：一条已发出、尚未收到 Response 的 Request。
 */
typedef struct XLinkPendingRequest {
    uint32_t m_requestId;    /**< 本端分配的关联 id。 */
    uint16_t m_command;      /**< 该 Request 的命令号（msgCode），用于匹配应答。 */
    uint32_t m_sendTick;     /**< 发出时的单调毫秒值，用于超时判定。 */
    bool     m_used;         /**< 槽位占用标志；false 表示空闲槽。 */
} XLinkPendingRequest;

/**
 * @brief 订阅表项：一个已向对端登记的 topicId。
 */
typedef struct XLinkTopicSubscription {
    uint32_t m_topicId; /**< 订阅的 topic（须 >= XLINK_TOPIC_USER_BASE）。 */
    bool     m_used;    /**< 槽位占用标志；false 表示空闲槽。 */
} XLinkTopicSubscription;

/**
 * @brief XLink 会话对象。
 * @details 持有帧接收缓冲、请求 id 发生器、待应答表、订阅表与握手状态。
 *          生命周期内仅在 init 时一次性申请接收缓冲，稳态收发路径零堆分配。
 */
typedef struct XLinkSession {
    XClass m_class;  /**< 基类；第一个成员、由 XClass 管理、禁止手工修改。 */

    XIODevice* m_device;  /**< 底层传输设备（借用，不取得所有权；可随时置 NULL 断开）。
                               类型不限：XTcpSocket/XSslSocket/回环设备均可。 */

    uint8_t* m_rxBuffer;  /**< 接收缓冲（对象拥有，XLINK_HEADER_SIZE + XLINK_MAX_PAYLOAD
                                字节，init 时经 XMemory 一次性申请）；仅供实现使用。 */
    uint8_t* m_txBuffer;  /**< 发送缓冲（对象拥有，XLINK_HEADER_SIZE + XLINK_MAX_PAYLOAD
                                字节，init 时经 XMemory 一次性申请）：单次原子写需帧头与
                                载荷连续，常驻缓冲避免大帧每次发送堆分配（稳态零堆）；
                                仅供实现使用。 */
    uint32_t m_rxFilled;  /**< 接收缓冲中已填充字节数（仅供实现使用）。 */
    uint32_t m_rxExpected; /**< 当前帧的期望总长度（仅供实现使用）：0 表示尚在等待帧头，
                                否则为帧头 12 字节 + 载荷长度。 */

    XLinkSessionState m_state; /**< 链路状态，见 XLinkSessionState。 */

    uint16_t m_localCapability;  /**< 本端声明的能力位，见 XLinkCapability。 */
    uint16_t m_remoteCapability; /**< 握手协商后的能力交集；未握手时为 0。 */
    uint16_t m_remoteVersion;    /**< 对端 Hello 携带的版本；未握手时为 0。 */

    uint32_t m_nextRequestId;      /**< 本端请求 id 发生器，从 1 起自增，0 视为无效并跳过。 */
    uint32_t m_nowTick;            /**< 最近一次 pump 传入的单调毫秒值（仅供实现使用）：
                                       发请求时据此打发送时间戳，避免本类自行取时。 */
    uint32_t m_lastSendTick;       /**< 最近一次发出任意帧的单调毫秒值。 */
    uint32_t m_lastReceiveTick;    /**< 最近一次收到任意字节的单调毫秒值。 */

    XLinkPendingRequest m_pending[XLINK_PENDING_SLOTS];      /**< 待应答表（定容，稳态零分配）。 */
    XLinkTopicSubscription m_subscriptions[XLINK_TOPIC_SLOTS]; /**< 订阅表（定容，稳态零分配）。 */

    XLinkSessionDispatchCallback m_dispatchCallback; /**< 消息分发回调（借用，可为 NULL）。 */
    void* m_userData;  /**< 调用方上下文（借用，仅供实现回传，不取得所有权）。 */
} XLinkSession;

/* ========================================================================== */
/*                        虚函数表初始化与构造                               */
/* ========================================================================== */

XVtable* XLinkSession_class_init(void);

/**
 * @brief      栈上初始化会话对象。
 * @param      obj 待初始化对象（调用方提供存储空间）；传入 NULL 时函数不执行任何操作。
 * @return     无。
 * @note       初始化后链路状态为 XLinkSessionState_Inactive，须再调用
 *             XLinkSession_setDevice 挂载传输才会进入 Connected。
 * @note       init 必须在 XLinkSession_dispatch_base 之前调用。
 */
void XLinkSession_init(XLinkSession* obj);

/**
 * @brief      创建堆会话对象（默认分配器）。
 * @return     成功返回新对象指针；内存分配失败返回 NULL。
 * @note       返回的对象须用 XClassDelete 释放。
 */
XLinkSession* XLinkSession_create(void);

/**
 * @brief      创建堆会话对象并挂载底层传输设备。
 * @param      device 传输设备（借用，不取得所有权，调用方保证其存活期覆盖本会话）。
 * @return     成功返回新对象指针；device 为 NULL 或分配失败返回 NULL。
 * @note       返回的对象须用 XClassDelete 释放。
 */
XLinkSession* XLinkSession_create_ex(XIODevice* device);

/* ========================================================================== */
/*                        虚函数调度入口                                     */
/* ========================================================================== */

/**
 * @brief      把一帧已解码的消息分派到 m_dispatchCallback。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      frame 帧头（借用）；传入 NULL 时函数不执行任何操作。
 * @param      payload 载荷首地址（借用，可为 NULL）。
 * @return     无。
 * @note       本函数只做空指针检查与虚表分派，业务逻辑在 VXLinkSession_dispatch 中。
 *             供内部 pump 与子类扩展共用；应用层通常不直接调用。
 */
void XLinkSession_dispatch_base(XLinkSession* self,
                                const XLinkFrameHeader* frame,
                                const uint8_t* payload);

/* ========================================================================== */
/*                        传输挂载                                           */
/* ========================================================================== */

/**
 * @brief      挂载或更换底层传输设备。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      device 传输设备（借用，不取得所有权）；传 NULL 表示主动断开并回到
 *                     XLinkSessionState_Inactive。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       更换或摘除设备时会清空帧缓冲、待应答表与订阅表，避免旧连接的
 *             半截帧与悬挂请求污染新链路；失败时对象保持原状态。
 */
bool XLinkSession_setDevice(XLinkSession* self, XIODevice* device);

/**
 * @brief      断开当前链路并复位到 XLinkSessionState_Inactive。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @return     无。
 * @note       不释放也不关闭底层设备（设备归调用方所有）。
 */
void XLinkSession_reset(XLinkSession* self);

/* ========================================================================== */
/*                        收发                                               */
/* ========================================================================== */

/**
 * @brief      非阻塞泵：从传输设备抽出全部可读字节并逐帧解码分发。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      nowMs 当前单调毫秒值（来自 XDateTime_currentMSecsSinceEpoch）。
 * @return     无。
 * @details    单圈内抽干传输积压（与 XPlatformFbInput_posix 的移动压缩同口径：
 *             逐帧同步分发，中途不重入），保证收侧永不被慢消费方拖住。
 * @note       必须由持有者的事件循环周期调用；本函数不阻塞、不取时、不加锁。
 */
void XLinkSession_pump(XLinkSession* self, uint32_t nowMs);

/**
 * @brief      发送一帧消息（内部按帧头 + 载荷两段写出，不拼大缓冲）。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      frame 帧头（借用）；m_payloadLen 须 <= XLINK_MAX_PAYLOAD。
 * @param      payload 载荷首地址（借用，不取得所有权）；长度可为 0，此时允许为 NULL。
 * @return     成功返回 true；参数非法、未挂载设备或传输写入失败返回 false。
 * @note       失败时对象状态不变（半帧已写出属传输层现象，由对端超时兜底）。
 */
bool XLinkSession_send(XLinkSession* self, const XLinkFrameHeader* frame,
                       const uint8_t* payload);

/**
 * @brief      发送会话帧（Hello / HelloAck / Heartbeat / Goodbye）。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      code 会话子类型，见 XLinkSessionCode。
 * @param      payload 可选的附加载荷（借用）；可为 NULL。
 * @param      length 附加载荷长度（字节）；可为 0。
 * @return     成功返回 true；参数非法或发送失败返回 false。
 */
bool XLinkSession_sendSession(XLinkSession* self, XLinkSessionCode code,
                              const uint8_t* payload, uint32_t length);

/**
 * @brief      发送请求帧并登记待应答。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      command 命令号（线上 msgCode）。
 * @param      payload 请求载荷（借用，不取得所有权）；可为 NULL。
 * @param      length 载荷长度（字节）；可为 0。
 * @param      outRequestId 本端分配的关联 id（调用方提供存储空间）；可为 NULL。
 * @return     成功返回 true；参数非法、待应答表已满或发送失败返回 false。
 * @note       两端各自独立分配 id，可同时互发请求互不干扰。
 */
bool XLinkSession_sendRequest(XLinkSession* self, uint16_t command,
                              const uint8_t* payload, uint32_t length,
                              uint32_t* outRequestId);

/**
 * @brief      发送应答帧，回带对端的关联 id。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      requestId 原样回带对端 Request 的关联 id。
 * @param      command 与请求相同的命令号（线上 msgCode）。
 * @param      payload 应答载荷（借用，不取得所有权）；可为 NULL。
 * @param      length 载荷长度（字节）；可为 0。
 * @return     成功返回 true；参数非法或发送失败返回 false。
 */
bool XLinkSession_sendResponse(XLinkSession* self, uint32_t requestId,
                               uint16_t command, const uint8_t* payload,
                               uint32_t length);

/**
 * @brief      发送推送帧（不等应答，产生即发）。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      topicId 主题号（线上 msgCode），须 >= XLINK_TOPIC_USER_BASE。
 * @param      payload 推送载荷（借用，不取得所有权）；可为 NULL。
 * @param      length 业务载荷长度（字节）；可为 0。
 * @return     成功返回 true；参数非法或发送失败返回 false。
 * @note       线上载荷 = [u32 topicId][业务载荷]，故业务载荷上限为
 *             XLINK_MAX_PAYLOAD - 4（XLINK_MAX_PAYLOAD 约束线上载荷总量）。
 *             超出上限直接返回 false，不截断、不部分发送。
 */
bool XLinkSession_sendPublish(XLinkSession* self, uint32_t topicId,
                              const uint8_t* payload, uint32_t length);

/* ========================================================================== */
/*                        订阅                                               */
/* ========================================================================== */

/**
 * @brief      向对端登记订阅某个 topic，并发 Subscribe 帧。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      topicId 主题号，须 >= XLINK_TOPIC_USER_BASE。
 * @return     成功返回 true；topicId 越界、订阅表已满或发送失败返回 false。
 */
bool XLinkSession_subscribe(XLinkSession* self, uint32_t topicId);

/**
 * @brief      取消订阅某个 topic，并发 Unsubscribe 帧。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      topicId 主题号，须 >= XLINK_TOPIC_USER_BASE。
 * @return     成功返回 true；topicId 越界或发送失败返回 false。
 * @note       该 topic 未在本地订阅表中时返回 false（不向对端发无意义的帧）。
 */
bool XLinkSession_unsubscribe(XLinkSession* self, uint32_t topicId);

/**
 * @brief      判断本地是否已订阅某个 topic。
 * @param      self 会话对象（借用）；传入 NULL 时返回 false。
 * @param      topicId 主题号。
 * @return     已订阅返回 true，否则返回 false。
 */
bool XLinkSession_isSubscribed(const XLinkSession* self, uint32_t topicId);

/* ========================================================================== */
/*                        待应答查询                                         */
/* ========================================================================== */

/**
 * @brief      判断某个请求是否仍在等待应答。
 * @param      self 会话对象（借用）；传入 NULL 时返回 false。
 * @param      requestId 关联 id。
 * @return     仍在待应答表中返回 true，否则返回 false。
 */
bool XLinkSession_isPending(const XLinkSession* self, uint32_t requestId);

/**
 * @brief      取出并移除最早的一条超时未应答请求（供 UI 提示"设备无响应"）。
 * @param      self 会话对象；传入 NULL 时不执行任何操作。
 * @param      nowMs 当前单调毫秒值（来自 XDateTime_currentMSecsSinceEpoch）。
 * @param      timeoutMs 判定超时的时长阈值（毫秒）。
 * @param      outCommand 超时请求的命令号（调用方提供存储空间）；可为 NULL。
 * @return     取到一条超时请求返回 true；无超时请求返回 false。
 * @note       取出的槽位立即出表，不会重复上报。
 */
bool XLinkSession_takeTimedOut(XLinkSession* self, uint32_t nowMs,
                               uint32_t timeoutMs, uint16_t* outCommand);

/* ========================================================================== */
/*                        属性                                               */
/* ========================================================================== */

/**
 * @brief      设置消息分发回调。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      callback 回调（借用，不取得所有权）；可为 NULL 表示取消分发。
 * @param      userData 调用方上下文（借用，仅原样回传给回调）。
 * @return     成功返回 true；self 为 NULL 返回 false。
 */
bool XLinkSession_setDispatchCallback(XLinkSession* self,
                                      XLinkSessionDispatchCallback callback,
                                      void* userData);

/**
 * @brief      设置本端声明的能力位（须在握手前调用）。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      capability 能力位组合，见 XLinkCapability。
 * @return     成功返回 true；self 为 NULL 返回 false。
 */
bool XLinkSession_setCapability(XLinkSession* self, uint16_t capability);

/**
 * @brief      获取握手协商后的能力交集。
 * @param      self 会话对象（借用）；传入 NULL 时返回 0。
 * @return     能力位组合；未握手完成时返回 0。
 */
uint16_t XLinkSession_negotiatedCapability(const XLinkSession* self);

/**
 * @brief      获取对端上报的协议版本。
 * @param      self 会话对象（借用）；传入 NULL 时返回 0。
 * @return     对端版本号；未收到对端 Hello 时返回 0。
 */
uint16_t XLinkSession_remoteVersion(const XLinkSession* self);

/**
 * @brief      获取当前链路状态。
 * @param      self 会话对象（借用）；传入 NULL 时返回 XLinkSessionState_Inactive。
 * @return     链路状态，见 XLinkSessionState。
 */
XLinkSessionState XLinkSession_state(const XLinkSession* self);

/**
 * @brief      判断链路是否已握手完成且处于健康状态。
 * @param      self 会话对象（借用）；传入 NULL 时返回 false。
 * @return     状态为 XLinkSessionState_Ready 时返回 true。
 */
bool XLinkSession_isReady(const XLinkSession* self);

#endif /* XLINK_ON && XLINK_SESSION_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKSESSION_H */
