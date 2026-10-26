/** @file       XLinkSession.h
 * @brief      XLink 私有协议会话对象公开 API（收发/分发/订阅/待应答/心跳）。
 * @details    对齐 Qt QTcpSocket + QMqttClient 的组合角色：对上表现为一条可读写
 *             的字节流，对下管理帧边界、消息分发、请求应答关联与链路心跳。
 *             实现只依赖 XinYueC 抽象层（XIODevice/XObject/XMemory/XDateTime），
 *             禁止调用 Win32、POSIX 或其他平台 API。
 *
 *             两端对称：同一套编解码、同一张分发表、同一份待应答表，
 *             "服务器端"与"客户端端"的唯一差别是建立链路时谁 accept、谁 connect
 *             ——传输建立之后协议层不再区分身份。因此本类既可驱动控制进程
 *             （被连接方），也可驱动 GUI 进程（主动连接方）。
 *
 *             双驱动口径（可并存）：会话继承 XObject——
 *               异步模式：setDevice 挂载传输时自动接线传输 readyRead 信号
 *               （收包事件驱动），并启动 XObject 内建定时器（XLINK_HEARTBEAT_TICK_MS
 *               节拍，驱动心跳发送与静默判线）；时间由本类经
 *               XDateTime_currentMSecsSinceEpoch()（单调毫秒）内部取，事件循环
 *               里无需任何手动调用。挂载传输后首次驱动还会自动补发 Hello
 *               （手动发过则跳过），握手全程零样板。
 *               手动模式：调用方按圈调 XLinkSession_pump(nowMs) 注入单调毫秒
 *               （环回设备不发 readyRead、测试注入固定时钟、无事件分发器的
 *               裸主循环均走此路径）。pump 幂等，两种驱动混用无副作用。
 *
 *             线程模型：单线程主循环口径，与 XPlatformFbInput_posix 的 fbdev
 *             显示/输入驱动一致——所有 API 均不可阻塞，须在持有者的事件循环
 *             （每圈 poll 回调或 XSocketNotifier）内调用，全类不加锁。
 *
 *             时间口径：手动 pump 模式的超时/心跳时长由调用方以
 *             XDateTime_currentMSecsSinceEpoch()（单调毫秒）传入；异步模式的
 *             时间由本类内部经同一接口取（仓库时间源裁定：时长测量用单调毫秒）。
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
#include "XObject.h"
#include "XClass.h"
#include "XIODevice.h"

/* ========================================================================== */
/*                        虚函数表                                           */
/* ========================================================================== */

XCLASS_DEFINE_BEGING(XLinkSession)
/* 新增槽位从基类槽位总数开始编号，否则与 XObject 的槽位撞号（建表容量不足）。 */
XCLASS_DEFINE_ENUM(XLinkSession, dispatch) = XCLASS_VTABLE_GET_SIZE(XObject),
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
    /* 【布局收敛 2026-10-26】按对齐宽度降序排布 + 小成员相邻：
     * 原序（u32,u16,u32,bool,buf[128],u32,u8）留三处填充洞
     * （u16 后 2B、bool 后 3B、u8 后 3B），sizeof=152；现序
     * （u32,u32,u32,u16,u8,bool,buf[128]）零填充，sizeof=144。
     * 待应答表 8 槽 → 每会话省 64 字节。成员名与语义不变，
     * 调用点零改动。 */
    uint32_t m_requestId;    /**< 本端分配的关联 id。 */
    uint32_t m_sendTick;     /**< 发出（或最近一次自动重发）时的单调毫秒值，用于超时判定。 */
    uint32_t m_payloadLen;   /**< 快照长度（字节）：可重试时等于原始载荷长度
                                （空载荷为 0）；超过 XLINK_RETRY_PAYLOAD_MAX 的
                                请求置 0 表示不可重试。仅供实现使用。 */
    uint16_t m_command;      /**< 该 Request 的命令号（msgCode），用于匹配应答。 */
    uint8_t  m_retryCount;   /**< 已自动重发次数；0xFF 为"不可重试"哨兵
                                （超长载荷），永远不满足重试额度判定。
                                仅供实现使用。 */
    bool     m_used;         /**< 槽位占用标志；false 表示空闲槽。 */
    uint8_t  m_payload[XLINK_RETRY_PAYLOAD_MAX]; /**< 请求载荷快照（自动重发用，
                                仅快照长度不超过 XLINK_RETRY_PAYLOAD_MAX 的请求）；
                                仅供实现使用。 */
} XLinkPendingRequest;

/**
 * @brief 订阅表项：一个已向对端登记的 topicId。
 * @details 【布局收敛 2026-10-26】槽位占用位**不挤占 topicId**：
 *          topicId 是协议标识，用户 topic 可达 0xFFFFFFFF，位域截断会
 *          静默改写线上标识（topic 匹配错配）。故占用位改由会话级位图
 *          （XLinkSession::m_subscriptionUsed）承担，表项退化为纯 u32：
 *          16 槽由 128 字节降到 64 字节（每会话省 64 字节）。
 */
typedef struct XLinkTopicSubscription {
    uint32_t m_topicId; /**< 订阅的 topic（须 >= XLINK_TOPIC_USER_BASE）。 */
} XLinkTopicSubscription;

/**
 * @brief XLink 会话对象。
 * @details 持有帧接收缓冲、请求 id 发生器、待应答表、订阅表与握手状态。
 *          生命周期内仅在 init 时一次性申请接收缓冲，稳态收发路径零堆分配。
 */
typedef struct XLinkSession {
    XObject m_class;  /**< 基类；第一个成员、由 XObject 管理、禁止手工修改。 */

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

    XTimerId m_timerId;  /**< 异步心跳/判线定时器 id（XObject 内建定时器，挂载传输时
                              启动）；XTIMER_INVALID_ID 表示未启动（无事件分发器或
                              XLINK_HEARTBEAT_TICK_MS=0），会话退化为手动 pump 模式。
                              仅供实现使用。 */
bool m_helloSent : 1; /**< Hello 已发出标志：手动 sendSession(Hello) 成功或首次驱动
                              自动补发后置位，防重复握手；换设备/复位时清零。
                              仅供实现使用。 */
    uint8_t m_retryCount : 8; /**< 请求自动重发次数上限（仅供实现使用）：自动超时
                                     后按原 requestId 静默重发的最大次数；
                                     init 默认 XLINK_RETRY_MAX_DEFAULT，运行时经
                                     XLinkSession_setNumberOfRetries 调整。
                                     【位域 2026-10-26】与 m_helloSent 同占 2 字节
                                     单元（原两成员各带 3 字节填充 = 8 字节）。 */
    uint32_t m_requestTimeoutMs; /* 请求自动超时判定阈值（毫秒，仅供实现使用）：
                                      0 = 被动模式（默认，超时不由库自动处理）；
                                      大于 0 时 pump 内自动扫描待应答表：
                                      可重试且额度未满则静默重发，否则发射
                                      requestFinished(false) 终局并出表。
                                      init 默认 XLINK_REQUEST_TIMEOUT_MS，
                                      运行时经 XLinkSession_setRequestTimeout 调整。 */

    XLinkPendingRequest m_pending[XLINK_PENDING_SLOTS];      /**< 待应答表（定容，稳态零分配）。 */
    XLinkTopicSubscription m_subscriptions[XLINK_TOPIC_SLOTS]; /**< 订阅表（定容，稳态零分配）。 */
    uint32_t m_subscriptionUsed;  /**< 订阅表槽位占用位图（bit i = 第 i 槽占用）：
                                      占用位由会话级位图承担（表项退化为纯 u32，
                                      16 槽 128→64 字节）；XLINK_TOPIC_SLOTS≤32 时
                                      恰好占满一个 u32。仅供实现使用。 */

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
 * @note       挂载非 NULL 设备时自动进入异步驱动：接线传输的 readyRead 信号
 *             （收包事件驱动）并启动内建定时器（心跳/判线节拍）。无事件分发器
 *             或设备不发 readyRead（如环回设备）时自动退化为手动 pump 模式。
 *             能力位（setCapability）应在挂载后、事件循环转起来之前设置，
 *             自动 Hello 才能携带正确的能力位。
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
 *             同时承担自动握手（Connected 态首次驱动补发 Hello）、心跳发送
 *             与静默判线——异步模式（readyRead 信号/内建定时器）内部同样
 *             经本函数驱动，只是时间由库内取。
 * @note       手动模式须由持有者的事件循环周期调用；本函数不阻塞、不加锁，
 *             两种驱动混用安全（幂等）。
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
 * @note       手动发送 Hello 成功会置"已握手"标志，自动补发随即跳过——
 *             手动握手与自动握手互斥，不产生重复帧。
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
 * @note       重复订阅幂等：对已订阅的 topic 再次订阅返回 true，不重复占用
 *             定容表槽位、不向对端重发帧。
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

/**
 * @brief      设置请求自动重发次数上限（自动超时重试模式）。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      count 最大自动重发次数（0 = 不重试，超时直接终局上报；
 *                  与被动模式的区别须配合 setRequestTimeout 大于 0 才生效）。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       init 默认 XLINK_RETRY_MAX_DEFAULT（0）。重发沿用原 requestId 静默
 *             进行，不影响对端关联；仅当请求载荷不超过 XLINK_RETRY_PAYLOAD_MAX
 *             时才可重发（载荷快照进待应答表槽位），超长请求超时后直接终局。
 *             对已有待应答请求立即生效（判定发生在下次 pump 扫描时）。
 */
bool XLinkSession_setNumberOfRetries(XLinkSession* self, uint8_t count);

/**
 * @brief      获取请求自动重发次数上限。
 * @param      self 会话对象（借用）；传入 NULL 时返回 0。
 * @return     当前自动重发次数上限（次）。
 */
uint8_t XLinkSession_numberOfRetries(const XLinkSession* self);

/**
 * @brief      设置请求自动超时判定阈值（自动超时模式的总开关）。
 * @param      self 会话对象；传入 NULL 时函数不执行任何操作。
 * @param      ms 阈值（毫秒）；0 = 被动模式（默认）：库不自动处理超时，
 *                超时请求长期滞留待应答表（由 isPending 查询、断链时清空）；
 *                大于 0 时 pump 内自动扫描待应答表，对超时槽执行静默重发
 *                或终局上报。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       init 默认 XLINK_REQUEST_TIMEOUT_MS（0）。自动模式的终局结果
 *             经 requestFinished 信号消费（false = 超时终局），已被终局
 *             出表的请求不会再产生第二次上报。阈值从请求发出（或最近一次
 *             重发）时刻起算——未经 pump 就发出的请求自首次 pump 那一拍
 *             起算（sendRequest 不自行取时，时间戳为 0 时扫描跳过该拍
 *             不判超时）；严格大于阈值才算超时。
 */
bool XLinkSession_setRequestTimeout(XLinkSession* self, uint32_t ms);

/**
 * @brief      获取请求自动超时判定阈值。
 * @param      self 会话对象（借用）；传入 NULL 时返回 0。
 * @return     当前阈值（毫秒）；0 表示被动模式（不自动处理超时）。
 */
uint32_t XLinkSession_requestTimeout(const XLinkSession* self);

/* ========================================================================== */
/*                        信号                                               */
/* ========================================================================== */

/**
 * @brief 触发 stateChanged 信号（链路状态真变化时由内部自动发射）。
 * @param self 会话对象实例指针（非NULL）
 * @param state 迁移后的新链路状态，见 XLinkSessionState
 * @return 信号标识
 * @note 槽函数原型：void slot(XObject* sender, XVarList* args)，参数依次为
 *       XLinkSessionState state。仅状态真变化时发射（同态重复设置不重发）；
 *       发射点覆盖：setDevice 挂载（Inactive→Connected）、握手完成
 *       （→Ready）、reset/Goodbye/静默判线/协议违例断链（→Inactive）。
 *       应用层通常无需手动调用本函数，连接示例：
 *       XObject_connect_2((XObject*)s, XSignal(XLinkSession_stateChanged_signal), slot);
 */
void* XLinkSession_stateChanged_signal(XLinkSession* self, XLinkSessionState state);

/**
 * @brief 触发 requestFinished 信号（请求取得终局时由内部自动发射）。
 * @param self 会话对象实例指针（非NULL）
 * @param requestId 终局请求的关联 id（与 sendRequest 输出一致；重发不换 id）
 * @param command 终局请求的命令号（msgCode）
 * @param succeeded true = 收到对端应答（正常完成）；false = 自动超时终局
 *                  （重试额度用尽/不可重试/重发失败）
 * @return 信号标识
 * @note 槽函数原型：void slot(XObject* sender, XVarList* args)，参数依次为
 *       uint32_t requestId、uint16_t command、bool succeeded。仅终局发射一次：
 *       应答命中待应答表时发 true；漂移应答（无对应待应答槽）不发射；
 *       重复应答（槽已出表）不发射。自动超时终局（false）仅自动模式
 *       （setRequestTimeout 大于 0）下产生。连接示例：
 *       XObject_connect_2((XObject*)s, XSignal(XLinkSession_requestFinished_signal), slot);
 */
void* XLinkSession_requestFinished_signal(XLinkSession* self, uint32_t requestId,
                                          uint16_t command, bool succeeded);

#endif /* XLINK_ON && XLINK_SESSION_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKSESSION_H */
