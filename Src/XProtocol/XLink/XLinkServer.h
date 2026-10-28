/** @file       XLinkServer.h
 * @brief      XLink 服务端包装：监听 + 自动建会话 + 会话回收。
 * @details    对齐 Qt QTcpServer 的"监听 + newConnection"使用姿势，把 XLink
 *             服务端样板收敛为一个对象：
 *               - 内嵌 XTcpServer 监听（对象拥有），newConnection 信号到达即
 *                 接受连接并自动创建 XLinkSession，套用预设的能力位与分发
 *                 回调模板——应用不再手写 accept/create_ex/setCapability/
 *                 setCallback 四步样板；
 *               - 会话生命周期由服务端托管：会话回 Inactive（对端 Goodbye、
 *                 判线、协议违例、传输断开）后自动回收，应用无需逐会话善后；
 *               - 内建 XObject 定时器节拍驱动（XLINK_HEARTBEAT_TICK_MS），
 *                 异步模式下事件循环里无需任何手动调用；手动模式（回环设备、
 *                 无事件分发器）按圈调 XLinkServer_pump 即可，两种模式可并存。
 *             会话本身仍是标准 XLinkSession：多客户端并存，回调里按
 *             XLinkSession* 区分来源；需要区分各客户端的扩展上下文时，用
 *             notify 回调（Added/Removed）维护"会话指针 -> 应用数据"映射。
 *
 *             线程模型：单线程主循环口径，与会话一致——所有 API 均不可阻塞，
 *             须在持有者的事件循环内使用，全类不加锁。
 *             依赖说明：本类是可选的 TCP 便捷包装（XLINK_SERVER_ON 门控），
 *             协议核心 XLinkSession 仍与传输无关。
 * @author     XinYueC 团队
 */
#ifndef XLINKSERVER_H
#define XLINKSERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XLink_config.h"

#if XLINK_ON && XLINK_SESSION_ON && XLINK_SERVER_ON

#include <stdint.h>
#include <stdbool.h>
#include "XLink_config.h"
#include "XLinkSession.h"
#include "XTcpServer.h"
#if XLINK_AUTH_ON
#include "XLinkUserStore.h"
#endif

/* ========================================================================== */
/*                        虚函数表                                           */
/* ========================================================================== */

XCLASS_DEFINE_BEGING(XLinkServer)
/* 新增槽位从基类槽位总数开始编号，否则与 XObject 的槽位撞号（建表容量不足）。 */
XCLASS_DEFINE_ENUM(XLinkServer, timerEvent) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XLinkServer)

/* ========================================================================== */
/*                        类型定义                                           */
/* ========================================================================== */

/* 服务端对象前置声明：下方通知回调 typedef 的参数需先指名 XLinkServer。 */
typedef struct XLinkServer XLinkServer;

/**
 * @brief 服务端通知事件：会话进入/离开服务端托管列表。
 */
typedef enum XLinkServerSessionEvent {
    XLinkServerSessionEvent_Added   = 0, /**< 新会话已建立（连接已接受，尚未握手）。 */
    XLinkServerSessionEvent_Removed = 1  /**< 会话已回收（Inactive，删除前回调，指针仍有效）。 */
} XLinkServerSessionEvent;

/**
 * @brief 会话增删通知回调：会话进入或离开服务端托管列表时调用。
 * @param self 服务端对象（借用）。
 * @param session 相关会话（借用）；Removed 事件在删除前回调，回调返回后指针失效。
 * @param event 事件类型，见 XLinkServerSessionEvent。
 * @return 无。
 * @note 回调在服务端 pump/accept 栈上同步执行；不得反调本服务的阻塞接口，
 *       不得在回调中删除会话（回收由服务端托管）。
 */
typedef void (*XLinkServerNotifyCallback)(XLinkServer* self,
                                          XLinkSession* session,
                                          XLinkServerSessionEvent event);

/**
 * @brief 服务端周期业务回调：由服务端内建节拍定时器在事件循环线程触发
 *        （每 XLINK_HEARTBEAT_TICK_MS 一次，随 XLinkServer_pump 驱动）。
 * @param self 服务端对象（借用）。
 * @param userData 注册时传入的上下文（借用，原样回传）。
 * @return 无。
 * @note 周期业务（状态快照推送、超时巡检等）在此注册即可，无需应用自建
 *       定时器或在主循环里手写周期逻辑；回调内可安全调用全部会话接口。
 */
typedef void (*XLinkServerTickCallback)(XLinkServer* self, void* userData);

#if XLINK_AUTH_ON
/**
 * @brief 认证审计事件。
 */
typedef enum XLinkServerAuthEvent {
    XLinkServerAuthEvent_Success = 0, /**< 认证通过。 */
    XLinkServerAuthEvent_Failure = 1, /**< 认证失败。 */
    XLinkServerAuthEvent_Timeout = 2, /**< 认证超时。 */
    XLinkServerAuthEvent_Anonymous = 3 /**< 匿名连接（未启用认证或允许匿名）。 */
} XLinkServerAuthEvent;

/**
 * @brief 认证审计回调：登录成功/失败/超时/匿名接入时触发。
 * @param self 服务端对象（借用）。
 * @param session 相关会话（借用；回调返回后可能被回收，禁止保存）。
 * @param event 事件类型。
 * @param user 用户名（UTF-8，借用；失败时可能为空串）。
 * @param role 通过时的角色；失败时为 0。
 * @return 无。
 * @note 只在服务端做审计/日志/限流统计用；**禁止**在回调内打印口令内容
 *       （回调拿不到口令，仅用户名与结果）。
 */
typedef void (*XLinkServerAuthAuditCallback)(XLinkServer* self,
                                             XLinkSession* session,
                                             XLinkServerAuthEvent event,
                                             const char* user, uint8_t role);
#endif

/**
 * @brief XLink 服务端包装对象。
 * @details 拥有内嵌 XTcpServer 与全部托管会话；对象销毁时同步删除监听器
 *          与所有仍托管的会话。
 */
typedef struct XLinkServer {
    XObject m_class;  /**< 基类；第一个成员、由 XObject 管理、禁止手工修改。 */

    XTcpServer* m_listener;  /**< 内嵌监听器（对象拥有，init 时创建）。 */

    XVector* m_sessions;  /**< 托管会话列表（对象拥有，元素为 XLinkSession*）。
                               仅供实现使用。 */

uint16_t m_capability;  /**< 新会话的能力位模板（setCapability 预设）。 */
    XLinkSessionDispatchCallback m_dispatchCallback; /* 新会话的分发回调模板（借用）。 */
    void* m_userData;  /* 分发回调上下文（借用，原样透传给会话）。 */
    XLinkServerNotifyCallback m_notifyCallback; /* 会话增删通知回调（借用，可为 NULL）。 */
    XLinkServerTickCallback m_tickCallback; /* 周期业务回调（借用，可为 NULL）。 */
    void* m_tickUserData;  /* 周期业务回调上下文（借用，原样回传）。 */

    XTimerId m_timerId;  /* 内建节拍定时器 id（listen 时启动，驱动回收与手动模式
                              兜底 pump）；XTIMER_INVALID_ID 表示未启动（无事件
                              分发器时），退化为手动 pump。仅供实现使用。 */
    uint32_t m_tickIntervalMs;  /* 内建节拍周期（毫秒）：listen 时按它启动节拍
                                     定时器，运行中可经 XLinkServer_setTickInterval
                                     调整；0 表示不启动节拍（转手动 pump）。
                                     默认取 XLINK_HEARTBEAT_TICK_MS。仅供实现使用。 */
    int m_pumpDepth;     /* pump 重入深度（仅供实现使用）：仅最外层 pump 末尾
                              回收会话，保证回收点不在任何会话的调用栈上。 */

#if XLINK_AUTH_ON
    XLinkUserStore m_users;      /* 账户库（对象拥有）：setUserStorePath 加载/
                                    认证查表用；未配置认证时保持空库。 */
    char* m_userStorePath;       /* 账户库路径（对象拥有，UTF-8 深拷贝）；NULL
                                    表示未持久化。 */
    bool m_authEnabled;          /* 已启用登录认证（账户库非空且显式开启）。 */
    bool m_authAllowAnonymous;   /* 认证启用时是否仍允许匿名连接（默认否）。 */
    uint8_t m_authFailureLimit;  /* 单连接认证失败上限（模板值）。 */
    uint32_t m_authTimeoutMs;    /* 认证超时（模板值）。 */
    XLinkServerAuthAuditCallback m_authAudit; /* 认证审计回调（借用）。 */
    void* m_authAuditUserData;   /* 审计回调上下文（借用）。 */
#endif
#if XLINK_FILE_ON
    XLinkSessionFileCallback m_fileHandler; /* File 消息类处理回调（借用）：
                                               文件传输引擎挂接点。 */
    void* m_fileHandlerUserData;            /* 上述回调上下文（借用）。 */
    void (*m_sessionGone)(XLinkServer* self, XLinkSession* session,
                          void* userData);  /* 会话即将被删除的内部钩子
                                               （扩展模块清理借用指针用）。 */
    void* m_sessionGoneUserData;            /* 上述钩子上下文（借用）。 */
    void (*m_preSessionPump)(XLinkServer* self, XLinkSession* session,
                             void* userData); /* 每会话 pump **之前**的钩子：
                                                 扩展可在判线逻辑前续命。 */
    void* m_preSessionPumpUserData;         /* 上述钩子上下文（借用）。 */
#endif
} XLinkServer;

/* ========================================================================== */
/*                        虚函数表初始化与构造                               */
/* ========================================================================== */

XVtable* XLinkServer_class_init(void);

/**
 * @brief      栈上初始化服务端对象。
 * @param      obj 待初始化对象（调用方提供存储空间）；传入 NULL 时函数不执行任何操作。
 * @return     无。
 * @note       初始化后内嵌监听器已创建但未监听；须再调用 XLinkServer_listen。
 *             init 必须与 XClassDeinit 成对出现。
 */
void XLinkServer_init(XLinkServer* obj);

/**
 * @brief      创建堆服务端对象（默认分配器）。
 * @return     成功返回新对象指针；内存分配失败返回 NULL。
 * @note       返回的对象须用 XClassDelete 释放。
 */
XLinkServer* XLinkServer_create(void);

/* ========================================================================== */
/*                        监听与模板                                         */
/* ========================================================================== */

/**
 * @brief      开始监听。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      host 监听地址（UTF-8）；NULL 或空串表示所有接口。
 * @param      port 监听端口；0 表示自动分配。
 * @return     成功返回 true；未监听成功返回 false（错误详情经内嵌监听器查询）。
 * @note       监听成功后启动内建节拍定时器并接线 newConnection 信号；
 *             重复 listen 会先关闭前一次监听。
 */
bool XLinkServer_listen_base(XLinkServer* self, const char* host, uint16_t port);

/**
 * @brief      停止监听并回收全部托管会话（对象保持可用，可重新 listen）。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @return     无。
 */
void XLinkServer_close(XLinkServer* self);

/**
 * @brief      设置新会话的能力位模板。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      capability 能力位组合，见 XLinkCapability。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       只影响之后建立的会话，不追溯已托管的会话。
 */
bool XLinkServer_setCapability(XLinkServer* self, uint16_t capability);

/**
 * @brief      设置新会话的消息分发回调模板。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      callback 回调（借用，不取得所有权）；可为 NULL 表示不分发。
 * @param      userData 调用方上下文（借用，原样透传给会话回调）。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       只影响之后建立的会话，不追溯已托管的会话。
 */
bool XLinkServer_setDispatchCallback(XLinkServer* self,
                                     XLinkSessionDispatchCallback callback,
                                     void* userData);

/**
 * @brief      设置会话增删通知回调。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      callback 回调（借用）；可为 NULL 表示取消通知。
 * @return     成功返回 true；self 为 NULL 返回 false。
 */
bool XLinkServer_setNotifyCallback(XLinkServer* self,
                                   XLinkServerNotifyCallback callback);

/**
 * @brief      注册周期业务回调（服务端内建节拍定时器驱动）。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      callback 周期业务回调（借用）；可为 NULL 表示取消。
 * @param      userData 回调上下文（借用，原样回传）。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       回调随 XLinkServer_pump 的内建节拍在事件循环线程触发
 *             （约每 XLINK_HEARTBEAT_TICK_MS 一次），状态推送等周期业务
 *             注册于此即可，无需应用自建定时器或手写主循环周期逻辑。
 */
bool XLinkServer_setTickCallback(XLinkServer* self,
                                 XLinkServerTickCallback callback,
                                 void* userData);

/**
 * @brief      读取当前 tick 回调（供扩展模块做链式保存/还原）。
 * @param      self 服务端对象；NULL 时不执行任何操作。
 * @param[out] outCallback 输出：当前 tick 回调（可能为 NULL）。
 * @param[out] outUserData 输出：当前 tick 用户数据（可能为 NULL）。
 * @note       扩展模块（如 XLinkFileServer）占用服务端唯一 tick 槽位，
 *             attach 前保存、detach 时还原，避免业务的周期回调被吞。
 */
void XLinkServer_tickCallback_const(XLinkServer* self,
                                    XLinkServerTickCallback* outCallback,
                                    void** outUserData);

/**
 * @brief      调整服务端内建节拍周期（运行中即时生效）。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      intervalMs 新周期（毫秒）；0 表示停掉内建节拍（tick 回调与
 *                     自动回收停止，转手动 XLinkServer_pump 驱动）。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       正在监听时立即按新周期重启节拍定时器，业务无感；未监听时
 *             仅记录，下次 listen 按新周期启动。周期越小 tick 回调与
 *             会话回收越及时，CPU 占用略增；一般 100~500ms 合理。
 */
bool XLinkServer_setTickInterval(XLinkServer* self, uint32_t intervalMs);

/**
 * @brief      获取当前内建节拍周期。
 * @param      self 服务端对象（借用）；传入 NULL 时返回 0。
 * @return     节拍周期（毫秒）；0 表示节拍已停（手动 pump 模式）。
 */
uint32_t XLinkServer_tickInterval(const XLinkServer* self);

/**
 * @brief      一对多发布：向所有"已订阅该 topic 且握手完成"的会话推送。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      topicId 主题号，须 >= XLINK_TOPIC_USER_BASE。
 * @param      payload 推送载荷（借用，不取得所有权）；可为 NULL。
 * @param      length 业务载荷长度（字节）；可为 0。
 * @return     实际推送到的会话数；topicId 越界返回 0。
 * @note       订阅关系由服务端自动维护：会话层收到对端的 Subscribe/
 *             Unsubscribe 帧时自动登记/移除（内部镜像到会话订阅表），
 *             应用无需记录"哪个客户端订了什么"。无人订阅时静默为 0，
 *             不视为错误。
 */
int XLinkServer_publish(XLinkServer* self, uint32_t topicId,
                        const uint8_t* payload, uint32_t length);

/**
 * @brief      广播：向所有握手完成的会话推送（不管是否订阅）。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      topicId 主题号，须 >= XLINK_TOPIC_USER_BASE。
 * @param      payload 推送载荷（借用，不取得所有权）；可为 NULL。
 * @param      length 业务载荷长度（字节）；可为 0。
 * @return     实际推送到的会话数；topicId 越界返回 0。
 * @note       与 XLinkServer_publish 的区别：广播不检查订阅关系，
 *             适合"全员必达"的系统级消息（如服务端即将重启的通知）。
 */
int XLinkServer_broadcast(XLinkServer* self, uint32_t topicId,
                          const uint8_t* payload, uint32_t length);

/* ========================================================================== */
/*                        驱动与查询                                         */
/* ========================================================================== */

/**
 * @brief      非阻塞泵：接受待处理连接、逐会话 pump、回收 Inactive 会话。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      nowMs 当前单调毫秒值（来自 XDateTime_currentMSecsSinceEpoch）。
 * @return     无。
 * @details    异步模式下会话各自由 readyRead/内建定时器驱动，本函数仍可安全
 *             调用（幂等），主要用于：手动模式兜底驱动、无事件分发器环境、
 *             以及希望减小回收延迟的手动节拍场景。重入安全：仅最外层 pump
 *             末尾执行回收，保证回收点不在任何会话的调用栈上。
 * @note       本函数不阻塞、不加锁；异步模式下可完全不调用（内建定时器
 *             代为驱动），回收延迟不超过 XLINK_HEARTBEAT_TICK_MS。
 */
void XLinkServer_pump(XLinkServer* self, uint32_t nowMs);

/**
 * @brief      获取当前托管会话数。
 * @param      self 服务端对象（借用）；传入 NULL 时返回 0。
 * @return     托管会话数量（含已连接但未握手完成的会话）。
 */
int XLinkServer_sessionCount(const XLinkServer* self);

/**
 * @brief      按下标取托管会话。
 * @param      self 服务端对象（借用）；传入 NULL 时返回 NULL。
 * @param      index 下标，范围 [0, XLinkServer_sessionCount)。
 * @return     会话指针（借用，仅当前时刻有效；回收后指针失效）；越界返回 NULL。
 */
XLinkSession* XLinkServer_sessionAt(const XLinkServer* self, int index);

/**
 * @brief      获取内嵌监听器（高级配置入口）。
 * @param      self 服务端对象（借用）；传入 NULL 时返回 NULL。
 * @return     内嵌 XTcpServer 指针（借用，归服务端所有，禁止删除）；
 *             可用于设置 maxPendingConnections、backlog 等。
 */
XTcpServer* XLinkServer_tcpServer_const(const XLinkServer* self);

#if XLINK_AUTH_ON

/* ========================================================================== */
/*                        登录认证（XLINK_AUTH_ON）                          */
/* ========================================================================== */

/**
 * @brief      启用/关闭登录认证。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      enabled 是否要求新会话认证。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       启用后：
 *               - 新会话能力位模板自动 OR 上 XLinkCapability_Auth；
 *               - 新会话握手完成后进入 Authenticating，认证通过才转 Ready；
 *               - 存量会话不受影响（在线连接不会被踢）。
 *             账户库为空时启用无意义：所有登录都会失败（文档已注明）。
 */
bool XLinkServer_setAuthEnabled(XLinkServer* self, bool enabled);

/**
 * @brief      查询是否启用登录认证。
 * @param      self 服务端对象（借用）；传入 NULL 时返回 false。
 * @return     已启用返回 true。
 */
bool XLinkServer_authEnabled(const XLinkServer* self);

/**
 * @brief      设置账户库文件路径并立即加载。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      path UTF-8 路径；NULL 或空串表示清除路径（账户库保持内存态）。
 * @return     加载成功返回 true；文件损坏/路径非法返回 false（库保持清空）。
 * @note       文件不存在视为成功（空库），调用方随后用 addUser 建账户并
 *             saveUserStore 落盘。路径深拷贝保存。
 */
bool XLinkServer_setUserStorePath(XLinkServer* self, const char* path);

/**
 * @brief      把当前账户库写回已设置的路径。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @return     成功返回 true；未设置路径或写入失败返回 false。
 */
bool XLinkServer_saveUserStore(XLinkServer* self);

/**
 * @brief      添加或更新账户（内存态；需 saveUserStore 才持久化）。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      user 用户名（UTF-8，非空）。
 * @param      password 口令明文（借用，不保存）；NULL/空串表示不改口令。
 * @param      role 角色，见 XLinkAuthRole。
 * @return     成功返回 true；库满/参数非法返回 false。
 * @note       新建账户必须给口令；账户口令以随机盐 + 迭代 HMAC 摘要保存，
 *             明文不留存。添加首个账户不会自动启用认证（须显式
 *             setAuthEnabled(true)，避免"加了个账户就连不上"）。
 */
bool XLinkServer_addUser(XLinkServer* self, const char* user,
                         const char* password, uint8_t role);

/**
 * @brief      删除账户（内存态）。
 * @param      self 服务端对象；user 用户名。
 * @return     删除成功返回 true；不存在返回 false。
 */
bool XLinkServer_removeUser(XLinkServer* self, const char* user);

/**
 * @brief      锁定/解锁账户（内存态）。
 * @param      self 服务端对象；user 用户名；locked 是否锁定。
 * @return     成功返回 true；用户不存在返回 false。
 */
bool XLinkServer_setUserLocked(XLinkServer* self, const char* user, bool locked);

/**
 * @brief      获取账户数量。
 * @param      self 服务端对象（借用）；传入 NULL 时返回 0。
 * @return     账户数。
 */
uint32_t XLinkServer_userCount(const XLinkServer* self);

/**
 * @brief      按下标查询账户（只读视图，供管理界面展示）。
 * @param      self 服务端对象（借用）。
 * @param      index 下标 [0, userCount)。
 * @return     借用指针；越界返回 NULL。
 * @note       返回结构含盐与摘要，展示层不应把摘要明文输出到界面/日志。
 */
const XLinkUserRecord* XLinkServer_userAt(const XLinkServer* self,
                                          uint32_t index);

/**
 * @brief      设置认证启用时是否允许匿名连接。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      allow 是否允许（默认 false：启用认证即全员须登录）。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       开启后，协商能力含 Auth 的对端仍会收到挑战；不宣告 Auth 的
 *             老对端按匿名放行（能力位门控的兼容语义）。
 */
bool XLinkServer_setAuthAllowAnonymous(XLinkServer* self, bool allow);

/**
 * @brief      设置单连接认证失败上限（只影响之后建立的会话）。
 * @param      self 服务端对象；limit >= 1（0 归一为 1）。
 * @return     成功返回 true；self 为 NULL 返回 false。
 */
bool XLinkServer_setAuthFailureLimit(XLinkServer* self, uint8_t limit);

/**
 * @brief      设置认证超时（只影响之后建立的会话）。
 * @param      self 服务端对象；ms 毫秒，0 = 不检查。
 * @return     成功返回 true；self 为 NULL 返回 false。
 */
bool XLinkServer_setAuthTimeout(XLinkServer* self, uint32_t ms);

/**
 * @brief      注册认证审计回调。
 * @param      self 服务端对象；callback 借用，可为 NULL 取消。
 * @param      userData 回调上下文（借用）。
 * @return     成功返回 true；self 为 NULL 返回 false。
 */
bool XLinkServer_setAuthAuditCallback(XLinkServer* self,
                                      XLinkServerAuthAuditCallback callback,
                                      void* userData);

/**
 * @brief      查询某个在线会话的登录用户。
 * @param      self 服务端对象（借用）。
 * @param      session 会话（借用；可为 NULL）。
 * @return     已认证时返回借用用户名；未认证/匿名返回 NULL。
 */
const char* XLinkServer_sessionUser_const(const XLinkServer* self,
                                          const XLinkSession* session);

/**
 * @brief      查询某个在线会话的角色。
 * @param      self 服务端对象（借用）。
 * @param      session 会话（借用；可为 NULL）。
 * @return     角色，见 XLinkAuthRole；未认证返回 Anonymous。
 */
uint8_t XLinkServer_sessionRole(const XLinkServer* self,
                                const XLinkSession* session);

#endif /* XLINK_AUTH_ON */

#if XLINK_FILE_ON

/* ========================================================================== */
/*                        文件传输挂接（XLINK_FILE_ON）                      */
/* ========================================================================== */

/**
 * @brief      注册 File 消息类处理回调（文件传输引擎挂接点）。
 * @param      self 服务端对象；传入 NULL 时函数不执行任何操作。
 * @param      callback 回调（借用）；服务端会把新会话的 File 帧转给它。
 * @param      userData 回调上下文（借用）。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       只影响之后建立的会话（模板下发）；已托管会话不受影响。
 *             服务端启用文件传输时，能力位模板会自动补上
 *             XLinkCapability_FileTransfer（由引擎在 attach 时设置）。
 */
bool XLinkServer_setFileCallback(XLinkServer* self,
                                 XLinkSessionFileCallback callback,
                                 void* userData);

/**
 * @brief      注册"会话即将被删除"内部钩子（扩展模块清理借用指针用）。
 * @param      self 服务端对象；NULL 时无操作。
 * @param      callback 钩子（借用）；在会话**删除前**调用，此时指针有效。
 * @param      userData 钩子上下文（借用）。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       每个服务端只保留一个钩子（后注册覆盖先注册）。文件传输
 *             扩展用它把会话槽里的借用指针在会话销毁前清掉，避免悬垂。
 */
bool XLinkServer_setSessionGoneHook(XLinkServer* self,
                                    void (*callback)(XLinkServer* self,
                                                     XLinkSession* session,
                                                     void* userData),
                                    void* userData);

/**
 * @brief      注册"会话 pump 之前"钩子（扩展模块续命用）。
 * @param      self 服务端对象；NULL 时无操作。
 * @param      callback 钩子（借用）；在每个托管会话 pump 之前调用。
 * @param      userData 钩子上下文（借用）。
 * @return     成功返回 true；self 为 NULL 返回 false。
 * @note       文件传输扩展用它为活跃传输的会话续命：会话 pump 内含静默
 *             判线，若等 tick 回调（pump 末尾）再续命就已判线断链。
 */
bool XLinkServer_setPreSessionPumpHook(XLinkServer* self,
                                       void (*callback)(XLinkServer* self,
                                                        XLinkSession* session,
                                                        void* userData),
                                       void* userData);

#endif /* XLINK_FILE_ON */

#endif /* XLINK_ON && XLINK_SESSION_ON && XLINK_SERVER_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKSERVER_H */
