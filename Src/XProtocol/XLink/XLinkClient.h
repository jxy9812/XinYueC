/** @file       XLinkClient.h
 * @brief      XLink 客户端包装：内嵌 XTcpSocket + XLinkSession + 自动重连。
 * @details    对齐 Qt QTcpSocket + QModbusClient 的"客户端"使用姿势，把 XLink
 *             客户端样板收敛为一个对象（与服务端包装 XLinkServer 对偶）：
 *               - 内嵌 XTcpSocket（对象拥有）发起异步连接，socket 的
 *                 connected/disconnected 信号自动接线；
 *               - TCP 连上才自动创建 XLinkSession 并套用预设的能力位与
 *                 分发回调模板，会话挂载传输后自动握手（零样板 Hello），
 *                 应用只等 Ready；
 *               - 会话 Ready 即本类 ConnectedState，经 stateChanged 信号
 *                 通知（对齐 XModbusClient_timeoutChanged_signal 的
 *                 XEmitSignal 发射模式，不用裸回调）；
 *               - 可选自动重连（默认关，对齐 QModbusClient::setAutoReconnect
 *                 默认值）：曾连上后链路断开则拆旧会话与套接字，转
 *                 ReconnectingState 并按 reconnectInterval 周期重试，达到
 *                 maxReconnectAttempts（-1 表示无限）后转 UnconnectedState
 *                 收手；手动 disconnectFromServer 不触发重连。
 *
 *             线程模型：单线程主循环口径，与会话/服务端一致——所有 API 均
 *             不可阻塞，须在持有者的事件循环内使用，全类不加锁。
 *             依赖说明：本类是可选的 TCP 便捷包装（XLINK_CLIENT_ON 门控），
 *             协议核心 XLinkSession 仍与传输无关。
 * @author     XinYueC 团队
 */
#ifndef XLINKCLIENT_H
#define XLINKCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XLink_config.h"

#if XLINK_ON && XLINK_SESSION_ON && XLINK_CLIENT_ON

#include <stdint.h>
#include <stdbool.h>
#include "XLinkSession.h"
#include "XTcpSocket.h"
#if XLINK_AUTH_ON
#include "XString.h"
#endif

/* ========================================================================== */
/*                        虚函数表                                           */
/* ========================================================================== */

XCLASS_DEFINE_BEGING(XLinkClient)
/* 新增槽位从基类槽位总数开始编号，否则与 XObject 的槽位撞号（建表容量不足）。 */
XCLASS_DEFINE_ENUM(XLinkClient, timerEvent) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XLinkClient)

/* ========================================================================== */
/*                        类型定义                                           */
/* ========================================================================== */

/**
 * @brief 客户端连接状态。
 * @details 状态迁移（对齐 QModbusDevice::State 命名）：
 *          Unconnected --(connectToServer)--> Connecting --(会话 Ready)-->
 *          Connected --(链路断开且自动重连开)--> Reconnecting --(重连成功)-->
 *          Connected；任一状态下链路断开且自动重连关、或重连次数耗尽、
 *          或手动 disconnectFromServer，均回到 Unconnected。
 *          Connected 即"内嵌会话已握手完成（XLinkSessionState_Ready）"，
 *          只有此态可收发业务消息。
 */
typedef enum XLinkClientState {
    XLinkClient_UnconnectedState  = 0, /**< 未连接（初始态；连接失败/手动断开/重连耗尽）。 */
    XLinkClient_ConnectingState   = 1, /**< 首次连接进行中（TCP 已发起，会话未 Ready）。 */
    XLinkClient_ConnectedState    = 2, /**< 已连接：内嵌会话握手完成（Ready），可收发业务消息。 */
    XLinkClient_ReconnectingState = 3  /**< 自动重连中：重连定时器按周期尝试恢复链路。 */
} XLinkClientState;

/**
 * @brief XLink 客户端包装对象。
 * @details 拥有内嵌 XTcpSocket 与当前 XLinkSession；对象销毁时同步拆除
 *          两者（销毁路径不经信号发射栈，符合同步释放判据）。
 */
typedef struct XLinkClient {
    XObject m_class;  /**< 基类；第一个成员、由 XObject 管理、禁止手工修改。 */

    XString* m_host;  /**< 服务端主机名/地址（对象拥有，UTF-8 借用入、XString
                               深拷贝存储；setHostPort 设置，连接时取用）。 */

    uint16_t m_port;  /**< 服务端端口（setHostPort 设置，连接时取用）。 */

    uint16_t m_capability;  /**< 新会话的能力位模板（setCapability 预设）。 */
    XLinkSessionDispatchCallback m_dispatchCallback; /**< 新会话的分发回调模板（借用）。 */
    void* m_userData;  /**< 分发回调上下文（借用，原样透传给会话回调）。 */

uint32_t m_reconnectInterval;     /**< 重连间隔（毫秒，默认 1000）。 */
    int16_t m_maxReconnectAttempts;   /**< 最大重连次数（-1 表示无限，默认 -1）。 */
    int16_t m_reconnectAttempts;      /**< 当前重连尝试计数（仅供实现使用，
                                           连接成功后清零）。 */

    XTcpSocket* m_socket;   /* 内嵌套接字（对象拥有，init 时为 NULL，首次
                                 connectToServer 或重连尝试时创建）；仅供实现使用。 */
    XLinkSession* m_session;  /* 当前会话（对象拥有，TCP 连上才创建，链路
                                   断开即拆除）；借用经 XLinkClient_session。
                                   仅供实现使用。 */

    XTimerId m_reconnectTimerId;  /* 重连定时器 id（进入 ReconnectingState 时
                                       启动，XObject 内建定时器）；XTIMER_INVALID_ID
                                       表示未启动。仅供实现使用。 */
    XLinkClientState m_state;  /* 当前连接状态，见 XLinkClientState。 */

    /* 【位域收敛 2026-10-26】三个布尔标志原本分散在结构体两端
     * （m_autoReconnect 在头、另两个在尾），各自带 3 字节对齐填充。
     * 改为位域并聚于尾部：bool 位域按 1 字节单元分配，三标志共占
     * 1 字节（省 2 字节 + 两处 3 字节填充洞）。成员名与语义不变，
     * 调用点零改动（bool 位域的读写/取址语义与普通 bool 一致，
     * 本结构无取址用法）。 */
    bool m_autoReconnect : 1;   /* 自动重连开关（默认 false，对齐 QModbusClient）。 */
    bool m_everReady : 1;       /* 曾达到 ConnectedState（会话 Ready）标志：
                                  自动重连只在该标志置位后生效。仅供实现使用。 */
    bool m_userDisconnect : 1;  /* 手动断开进行中标志：屏蔽拆除过程中的信号
                                  触发自动重连。仅供实现使用。 */
#if XLINK_AUTH_ON
    /* 认证相关标志同样收敛为位域（与上面三标志同一存储单元扩展）。 */
    bool m_authFailed : 1;      /* 最近一次连接因认证失败而终止：阻断自动重连
                                   （口令错误属永久失败）。仅供实现使用。 */
    XString* m_authUser;        /* 登录用户名（对象拥有，XString UTF-8 存储）。 */
    XString* m_authPassword;    /* 登录口令（对象拥有；XString 内部缓冲，
                                    清除/析构时先零化内容再释放）。 */
    int m_lastAuthFailure;      /* 最近一次认证失败原因（XLinkClientAuthFailure）。 */
#endif
} XLinkClient;

/* ========================================================================== */
/*                        虚函数表初始化与构造                               */
/* ========================================================================== */

XVtable* XLinkClient_class_init(void);

/**
 * @brief      栈上初始化客户端对象。
 * @param      obj 待初始化对象（调用方提供存储空间）；传入 NULL 时函数不执行任何操作。
 * @return     无。
 * @note       初始化后状态为 XLinkClient_UnconnectedState，主机名为空串、
 *             端口 0，须再调用 XLinkClient_setHostPort 与
 *             XLinkClient_connectToServer。init 必须与 XClassDeinit 成对出现。
 */
void XLinkClient_init(XLinkClient* obj);

/**
 * @brief      创建堆客户端对象（默认分配器）。
 * @return     成功返回新对象指针；内存分配失败返回 NULL。
 * @note       返回的对象须用 XClassDelete 释放。
 */
XLinkClient* XLinkClient_create(void);

/* ========================================================================== */
/*                        连接参数与模板                                     */
/* ========================================================================== */

/**
 * @brief      设置服务端地址与端口。
 * @param      self 客户端对象；传入 NULL 时函数不执行任何操作。
 * @param      host 服务端主机名或 IP（UTF-8，借用，不取得所有权）；NULL 或
 *                  空串视为非法，返回 false 且不修改原配置。
 * @param      port 服务端端口（主机字节序）。
 * @return     成功返回 true；self 或 host 为 NULL/空串返回 false。
 * @note       host 内部经 XString 深拷贝保存，调用返回后可立即释放或修改
 *             传入缓冲。配置只在下次 connectToServer/重连尝试时取用，
 *             已建立的链路不受影响。
 */
bool XLinkClient_setHostPort(XLinkClient* self, const char* host, uint16_t port);

/**
 * @brief      获取服务端主机名/地址（内部存储的只读视图）。
 * @param      self 客户端对象（借用）；传入 NULL 时返回 NULL。
 * @return     UTF-8 借用指针（指向内部 XString 的编码缓存，归对象所有，
 *             禁止释放或修改；下次 setHostPort 后失效）。
 */
const char* XLinkClient_host_const(const XLinkClient* self);

/**
 * @brief      获取服务端端口。
 * @param      self 客户端对象（借用）；传入 NULL 时返回 0。
 * @return     端口（主机字节序）；未设置时为 0。
 */
uint16_t XLinkClient_port(const XLinkClient* self);

/**
 * @brief      设置新会话的能力位模板。
 * @param      self 客户端对象；传入 NULL 时函数不执行任何操作。
 * @param      capability 能力位组合，见 XLinkCapability。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       只影响之后创建的会话（下次连接/重连成功时套用），不追溯
 *             当前会话。会话挂载传输后自动握手，能力位随 Hello 上线。
 */
bool XLinkClient_setCapability(XLinkClient* self, uint16_t capability);

/**
 * @brief      设置新会话的消息分发回调模板。
 * @param      self 客户端对象；传入 NULL 时函数不执行任何操作。
 * @param      callback 回调（借用，不取得所有权）；可为 NULL 表示不分发。
 * @param      userData 调用方上下文（借用，原样透传给会话回调）。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       只影响之后创建的会话（每次重连新建会话时重新套用），
 *             不追溯当前会话。
 */
bool XLinkClient_setDispatchCallback(XLinkClient* self,
                                     XLinkSessionDispatchCallback callback,
                                     void* userData);

/* ========================================================================== */
/*                        自动重连配置                                       */
/* ========================================================================== */

/**
 * @brief      设置是否启用自动重连。
 * @param      self 客户端对象；传入 NULL 时函数不执行任何操作。
 * @param      enabled 是否启用；默认 false（对齐 QModbusClient）。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       只对"曾达到 ConnectedState 后链路断开"的场景生效；禁用时
 *             立即停止进行中的重连定时器（下一次链路断开直接回
 *             UnconnectedState）。
 */
bool XLinkClient_setAutoReconnect(XLinkClient* self, bool enabled);

/**
 * @brief      查询是否启用自动重连。
 * @param      self 客户端对象（借用）；传入 NULL 时返回 false。
 * @return     已启用返回 true。
 */
bool XLinkClient_autoReconnect(const XLinkClient* self);

/**
 * @brief      设置重连间隔。
 * @param      self 客户端对象；传入 NULL 时函数不执行任何操作。
 * @param      intervalMs 重连间隔（毫秒）；默认 1000。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       间隔为 0 表示重连定时器节拍最密（事件循环每圈尝试），一般
 *             不建议；运行中改动从下一次重连尝试起生效。
 */
bool XLinkClient_setReconnectInterval(XLinkClient* self, uint32_t intervalMs);

/**
 * @brief      获取重连间隔。
 * @param      self 客户端对象（借用）；传入 NULL 时返回默认值 1000。
 * @return     重连间隔（毫秒）。
 */
uint32_t XLinkClient_reconnectInterval(const XLinkClient* self);

/**
 * @brief      设置最大重连次数。
 * @param      self 客户端对象；传入 NULL 时函数不执行任何操作。
 * @param      attempts 最大重连次数；-1 表示无限重试（默认），0 表示断线后
 *                     不尝试（等同自动重连关闭），正值为最多尝试次数。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       达到上限后转 XLinkClient_UnconnectedState 收手；连接成功后
 *             计数清零。
 */
bool XLinkClient_setMaxReconnectAttempts(XLinkClient* self, int16_t attempts);

/**
 * @brief      获取最大重连次数。
 * @param      self 客户端对象（借用）；传入 NULL 时返回默认值 -1。
 * @return     最大重连次数（-1 表示无限）。
 */
int16_t XLinkClient_maxReconnectAttempts(const XLinkClient* self);

/**
 * @brief      获取当前重连尝试计数。
 * @param      self 客户端对象（借用）；传入 NULL 时返回 0。
 * @return     本次失线以来已进行的重连尝试次数；连接成功后清零。
 */
int16_t XLinkClient_reconnectAttempts(const XLinkClient* self);

/* ========================================================================== */
/*                        连接与断开                                         */
/* ========================================================================== */

/**
 * @brief      异步发起连接（对齐 QModbusDevice::connectToDevice）。
 * @param      self 客户端对象；传入 NULL 时函数不执行任何操作。
 * @return     连接已发起返回 true；self 为 NULL、未设置主机名或内部套接字
 *             创建失败返回 false。
 * @note       异步：返回 true 只表示 TCP 连接已发起，须等 stateChanged 信号
 *             到 XLinkClient_ConnectedState（内嵌会话 Ready）才能收发业务
 *             消息。已处于连接态时重复调用为先拆旧链路再重连。首次连接
 *             失败（未达到过 ConnectedState）不自动重连，直接回
 *             UnconnectedState。
 */
bool XLinkClient_connectToServer(XLinkClient* self);

/**
 * @brief      优雅断开：发 Goodbye、排空写队列、拆除会话与套接字。
 * @param      self 客户端对象；传入 NULL 时函数不执行任何操作。
 * @return     无。
 * @details    若内嵌会话仍存活则先发 Goodbye（对端感知主动断链），再循环
 *             waitForBytesWritten 排空套接字写队列（套接字写入是异步管线，
 *             不排空就拆会把未 flush 的帧一起丢掉），最后同步拆除会话与
 *             套接字并转 XLinkClient_UnconnectedState。
 * @note       手动断开不触发自动重连；未连接时调用为安全空操作（状态仍
 *             归一到 UnconnectedState）。
 */
void XLinkClient_disconnectFromServer(XLinkClient* self);

/* ========================================================================== */
/*                        查询                                               */
/* ========================================================================== */

/**
 * @brief      获取当前会话（收发业务消息的入口）。
 * @param      self 客户端对象（借用）；传入 NULL 时返回 NULL。
 * @return     会话指针（借用，归客户端所有，禁止删除）；未连接或链路断开
 *             时为 NULL。指针只在当前连接存活期有效——重连会新建会话，
 *             旧指针随即失效。
 * @note       仅 XLinkClient_isReady 为 true 时才可经它收发业务消息。
 */
XLinkSession* XLinkClient_session(const XLinkClient* self);

/**
 * @brief      获取当前连接状态。
 * @param      self 客户端对象（借用）；传入 NULL 时返回 XLinkClient_UnconnectedState。
 * @return     连接状态，见 XLinkClientState。
 */
XLinkClientState XLinkClient_state(const XLinkClient* self);

/**
 * @brief      判断链路是否可用（可收发业务消息）。
 * @param      self 客户端对象（借用）；传入 NULL 时返回 false。
 * @return     状态为 XLinkClient_ConnectedState（内嵌会话 Ready）时返回 true。
 */
bool XLinkClient_isReady(const XLinkClient* self);

/* ========================================================================== */
/*                        信号接口                                           */
/* ========================================================================== */

/**
 * @brief      触发 stateChanged 信号（连接状态变更通知）。
 * @param      self 客户端实例指针（可为 NULL，为 NULL 时不发射）。
 * @param      state 变更后的状态。
 * @return     信号标识（发射写法对齐 XModbusClient_timeoutChanged_signal）。
 * @note       发射时机：Unconnected/Connecting/Connected/Reconnecting 之间
 *             的每次实际迁移。
 */
void* XLinkClient_stateChanged_signal(XLinkClient* self, XLinkClientState state);

#if XLINK_AUTH_ON

/* ========================================================================== */
/*                        登录凭据（XLINK_AUTH_ON）                          */
/* ========================================================================== */

/** @brief 客户端登录失败原因（authFailed 信号参数）。 */
typedef enum XLinkClientAuthFailure {
    XLinkClientAuthFailure_None = 0,        /**< 未失败。 */
    XLinkClientAuthFailure_BadUser = 1,     /**< 用户不存在。 */
    XLinkClientAuthFailure_BadPassword = 2, /**< 口令错误。 */
    XLinkClientAuthFailure_Locked = 3,      /**< 账户锁定。 */
    XLinkClientAuthFailure_Rejected = 4,    /**< 通用拒绝。 */
    XLinkClientAuthFailure_NoCredential = 5,/**< 对端要求认证但本端未设凭据。 */
    XLinkClientAuthFailure_Timeout = 6,     /**< 认证超时。 */
    XLinkClientAuthFailure_Proof = 7,       /**< 服务端证明校验失败（可疑对端）。 */
    XLinkClientAuthFailure_Protocol = 8     /**< 认证帧畸形。 */
} XLinkClientAuthFailure;

/**
 * @brief      设置登录用户名与口令（深拷贝保存）。
 * @param      self 客户端对象；传入 NULL 时函数不执行任何操作。
 * @param      user UTF-8 用户名（非空）。
 * @param      password UTF-8 口令明文（非空）。
 * @return     成功返回 true；参数非法或分配失败返回 false。
 * @note       设置后每次连接/重连成功都会自动登录；口令以明文驻留内存
 *             （协议需要它在本地派生摘要），clearCredential/析构时零化释放。
 *             口令不会出现在任何日志中。
 */
bool XLinkClient_setCredential(XLinkClient* self, const char* user,
                               const char* password);

/**
 * @brief      清除登录凭据（零化后释放）。
 * @param      self 客户端对象；传入 NULL 时函数不执行任何操作。
 * @return     无。
 * @note       清除后若对端要求认证，连接会快速失败（authFailed/
 *             NoCredential），不会挂起。
 */
void XLinkClient_clearCredential(XLinkClient* self);

/**
 * @brief      查询是否已设置登录凭据。
 * @param      self 客户端对象（借用）；传入 NULL 时返回 false。
 * @return     已设置返回 true。
 */
bool XLinkClient_hasCredential(const XLinkClient* self);

/**
 * @brief      查询登录用户名。
 * @param      self 客户端对象（借用）；传入 NULL 时返回 NULL。
 * @return     借用 UTF-8 指针；未设置返回 NULL。
 */
const char* XLinkClient_user_const(const XLinkClient* self);

/**
 * @brief      查询最近一次认证失败原因。
 * @param      self 客户端对象（借用）；传入 NULL 时返回 0。
 * @return     XLinkClientAuthFailure 值；未失败返回 0。
 * @note       authFailed 信号到达后查询，用于界面提示。
 */
int XLinkClient_lastAuthFailure(const XLinkClient* self);

/**
 * @brief      查询当前会话是否已通过认证。
 * @param      self 客户端对象（借用）；传入 NULL 时返回 false。
 * @return     已认证返回 true；未连接/认证未完成返回 false。
 */
bool XLinkClient_isAuthenticated(const XLinkClient* self);

/**
 * @brief      触发 authFailed 信号（认证失败终局时由内部发射）。
 * @param      self 客户端实例指针（可为 NULL，为 NULL 时不发射）。
 * @param      reason 失败原因，见 XLinkClientAuthFailure。
 * @param      status 协议层结果码（XLinkAuthStatus），便于诊断。
 * @return     信号标识。
 * @note       槽函数原型：void slot(XObject* sender, XVarList* args)，
 *             参数依次为 int reason、uint8_t status。认证失败会同时停止
 *             自动重连（口令错误重试无意义），状态转 UnconnectedState。
 */
void* XLinkClient_authFailed_signal(XLinkClient* self, int reason,
                                    uint8_t status);

#endif /* XLINK_AUTH_ON */

#endif /* XLINK_ON && XLINK_SESSION_ON && XLINK_CLIENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKCLIENT_H */
