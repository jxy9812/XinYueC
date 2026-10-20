/** @file       XNtpClient.h
 * @brief      XNtp 客户端——SNTP 口径的应用内校时客户端公开 API。
 * @details    对齐 Qt 的异步事件驱动风格（对标 QNtpClient 社区实现的接口
 *             形态）：配置服务器后 syncNow 发起异步同步，结果经信号通知。
 *             实现只依赖 XinYueC 抽象层（XUdpSocket/XHostInfo/XDateTime/
 *             XObject），禁止调用 Win32、POSIX 或其他平台 API。
 *
 *             协议行为（RFC 5905 SNTP 口径）：
 *             - 单次请求-应答：发出 48 字节客户端请求（携 T1），应答携
 *               T1 回显与服务器 T2/T3，据此计算时钟偏移与往返时延：
 *                   offset = ((T2-T1) + (T3-T4)) / 2
 *                   delay  = (T4-T1) - (T3-T2)
 *             - 应答校验：Mode 必须为 Server、Stratum 须在 1..15（0 为
 *               Kiss-o'-Death）、LI 不得为「未同步」、T1 回显必须一致、
 *               服务器时间戳必须非零。
 *             - 校时落地：默认不写系统时钟。同步成功后由应用显式调用
 *               XNtpClient_applyToSystem（内部走 XDateTime 设置系统时间
 *               接口，需 root/CAP_SYS_TIME/SE_SYSTEMTIME 特权）。
 *
 * @note       单线程事件循环口径：本类依赖事件循环分发 readyRead 与定时器，
 *             不可跨线程操作；setServer 涉及 DNS 解析（阻塞），请在启动
 *             阶段配置而非高频调用。
 */
#ifndef XNTPCLIENT_H
#define XNTPCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XNTP_ON / XNTP_CLIENT_ON 已定义
   （必须早于下方条件编译判定，否则直接 include 本头文件时开关未定义、
   整段声明会被跳过） */
#include "CXinYueConfig.h"
#include "XNtp_config.h"

#if XNTP_ON && XNTP_CLIENT_ON

#include "XObject.h"
#include "XString.h"
#include "XNtpProto.h"

/* ========================================================================== */
/*                        虚函数表                                           */
/* ========================================================================== */

XCLASS_DEFINE_BEGING(XNtpClient)
XCLASS_DEFINE_ENUM(XNtpClient, TimerEvent) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XNtpClient)

/* ========================================================================== */
/*                        类型定义                                           */
/* ========================================================================== */

/**
 * @brief 客户端状态。
 */
typedef enum XNtpClientState {
    XNtpClientState_Idle          = 0, /**< 未配置或从未同步。 */
    XNtpClientState_Syncing       = 1, /**< 同步进行中（等待应答/重试）。 */
    XNtpClientState_Synchronized  = 2, /**< 最近一次同步成功。 */
    XNtpClientState_Failed        = 3  /**< 最近一次同步失败（原因见 lastError）。 */
} XNtpClientState;

/**
 * @brief 同步失败原因。
 */
typedef enum XNtpClientError {
    XNtpClientError_None                  = 0, /**< 无错误。 */
    XNtpClientError_NoServer              = 1, /**< 未配置服务器或解析失败。 */
    XNtpClientError_Socket                = 2, /**< 套接字创建/发送失败。 */
    XNtpClientError_Timeout               = 3, /**< 应答超时（含重试耗尽）。 */
    XNtpClientError_BadPacket             = 4, /**< 报文非法（长度/格式）。 */
    XNtpClientError_ServerUnsynchronized  = 5, /**< 服务器时钟未同步（LI=3 或 stratum 16+）。 */
    XNtpClientError_KissOfDeath           = 6, /**< 服务器拒绝服务（stratum 0）。 */
    XNtpClientError_MismatchedOrigin      = 7  /**< T1 回显不匹配（错包/重放）。 */
} XNtpClientError;

/* 私有状态前置声明（定义在 XNtpClient_Protected.h，仅供实现使用）。 */
typedef struct XNtpClientPrivate XNtpClientPrivate;

/**
 * @brief XNtp 客户端对象。
 * @note  m_class 为第一个成员、由 XClass 管理、禁止手工修改。
 */
typedef struct XNtpClient {
    XObject m_class;            /**< 基类；第一个成员、由 XClass 管理、禁止手工修改。 */
    XNtpClientPrivate* m_d;     /**< 私有状态（对象拥有）；仅供实现使用。 */
} XNtpClient;

/* ========================================================================== */
/*                        虚函数表初始化与构造                               */
/* ========================================================================== */

XVtable* XNtpClient_class_init(void);

/**
 * @brief      栈上初始化客户端对象。
 * @param      obj 待初始化对象（调用方提供存储空间）；NULL 不执行任何操作。
 * @return     无。
 * @note       init 后对象为 Idle 状态，需先 setServer 再 syncNow。
 */
void XNtpClient_init(XNtpClient* obj);

/**
 * @brief      创建堆客户端对象。
 * @return     成功返回新对象指针；分配失败返回 NULL。须用 XClassDelete 释放。
 */
XNtpClient* XNtpClient_create(void);

/* ========================================================================== */
/*                        配置                                               */
/* ========================================================================== */

/**
 * @brief      配置 NTP 服务器。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      name 服务器名（域名或点分 IPv4，内部按 UTF-8 处理）。
 * @param      port 服务器端口，0 取 XNTP_DEFAULT_PORT(123)。
 * @return     成功返回 true；参数非法返回 false。
 * @note       会触发一次阻塞式 DNS 解析以缓存地址；解析失败不置错，
 *             syncNow 时会重试解析。启动阶段调用，勿在同步进行中调用。
 */
bool XNtpClient_setServer(XNtpClient* self, const XString* name, uint16_t port);

/**
 * @brief      配置 NTP 服务器（UTF-8 字符串便捷版）。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      nameUtf8 服务器名（UTF-8 编码）。
 * @param      port 服务器端口，0 取默认端口 123。
 * @return     成功返回 true；参数非法返回 false。
 */
bool XNtpClient_setServer_2(XNtpClient* self, const char* nameUtf8, uint16_t port);

/**
 * @brief      设置单次同步的应答超时。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      timeoutMs 超时毫秒数；0 取默认 XNTP_DEFAULT_TIMEOUT_MS。
 * @return     成功返回 true。
 */
bool XNtpClient_setTimeout(XNtpClient* self, uint32_t timeoutMs);

/**
 * @brief      设置自动周期同步。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      on true 启用周期同步，false 停止。
 * @param      intervalMs 周期毫秒数；0 取默认（60 分钟）。过短周期对公共
 *             NTP 服务器不礼貌，内网自建源可酌情缩短（建议 >= 30s）。
 * @return     成功返回 true。
 */
bool XNtpClient_setAutoSync(XNtpClient* self, bool on, uint32_t intervalMs);

/**
 * @brief      设置超时后的最大重试次数。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      retries 重试次数（不含首次发送）；负值视为 0。
 * @return     成功返回 true。
 */
bool XNtpClient_setRetries(XNtpClient* self, int retries);

/* ========================================================================== */
/*                        同步                                               */
/* ========================================================================== */

/**
 * @brief      立即发起一次同步（异步：发出请求后立即返回）。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @return     请求已发出返回 true；未配置服务器、套接字失败或正在同步中
 *             返回 false（状态不变）。
 * @note       结果经 synchronized/syncFailed 信号通知；成功后可用
 *             offsetMs/roundTripMs/serverSecs 查询详情。
 */
bool XNtpClient_syncNow(XNtpClient* self);

/* ========================================================================== */
/*                        查询                                               */
/* ========================================================================== */

/**
 * @brief      查询客户端状态。
 * @param      self 客户端实例（借用）；NULL 返回 XNtpClientState_Idle。
 */
XNtpClientState XNtpClient_state(const XNtpClient* self);

/**
 * @brief      查询最近一次失败原因。
 * @param      self 客户端实例（借用）；NULL 返回 XNtpClientError_None。
 */
XNtpClientError XNtpClient_lastError(const XNtpClient* self);

/**
 * @brief      是否至少成功同步过一次。
 * @param      self 客户端实例（借用）；NULL 返回 false。
 */
bool XNtpClient_isSynchronized(const XNtpClient* self);

/**
 * @brief      最近一次成功同步的时钟偏移（服务器时间 - 本地墙钟，毫秒）。
 * @param      self 客户端实例（借用）；NULL 或从未同步返回 0。
 * @note       正值表示本地时钟落后于服务器；仅供展示与决策，
 *             系统校时请走 XNtpClient_applyToSystem。
 */
int64_t XNtpClient_offsetMs(const XNtpClient* self);

/**
 * @brief      最近一次成功同步的网络往返时延（毫秒）。
 * @param      self 客户端实例（借用）；NULL 或从未同步返回 0。
 * @note       时延过大（跨百毫秒）说明偏移估计不可信，应用可据此丢弃本次
 *             结果或改用更近的服务器。
 */
int64_t XNtpClient_roundTripMs(const XNtpClient* self);

/**
 * @brief      最近一次成功同步的服务器 UTC 纪元秒。
 * @param      self 客户端实例（借用）；NULL 或从未同步返回 0。
 */
uint64_t XNtpClient_serverSecs(const XNtpClient* self);

/**
 * @brief      最近一次应答的服务器层级（stratum）。
 * @param      self 客户端实例（借用）；NULL 或从未同步返回 0。
 */
uint8_t XNtpClient_stratum(const XNtpClient* self);

/* ========================================================================== */
/*                        校时落地                                           */
/* ========================================================================== */

/**
 * @brief      把同步结果落盘到系统时钟（估算的当前真实时刻，秒级）。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @return     写入成功返回 true；从未同步、无权限或平台不支持返回 false，
 *             此时系统时钟保持不变。
 * @note       默认不自动调用——是否改系统时钟是策略决定，归应用所有。
 *             可先用 XDateTime_canSetSystemTime() 探测权限。
 * @warning    写系统时钟影响全系统（日志/文件时间戳/证书校验）。单调钟
 *             （XDateTime_currentMSecsSinceEpoch）不受影响，定时器/心跳安全。
 */
bool XNtpClient_applyToSystem(XNtpClient* self);

/* ========================================================================== */
/*                        信号                                               */
/* ========================================================================== */

/**
 * @brief 同步成功信号（发出后可查询 offsetMs/roundTripMs/serverSecs）。
 * @param client 客户端实例指针（非 NULL）。
 * @return 信号发送结果。
 */
void* XNtpClient_synchronized_signal(XNtpClient* client);

/**
 * @brief 同步失败信号（重试耗尽后发出，原因查 lastError）。
 * @param client 客户端实例指针（非 NULL）。
 * @return 信号发送结果。
 */
void* XNtpClient_syncFailed_signal(XNtpClient* client);

/**
 * @brief 偏移量更新信号（每次同步成功发出，参数为新偏移毫秒）。
 * @param client 客户端实例指针（非 NULL）。
 * @param offsetMs 新的时钟偏移（毫秒，服务器 - 本地）。
 * @return 信号发送结果。
 */
void* XNtpClient_offsetChanged_signal(XNtpClient* client, int64_t offsetMs);

#endif /* XNTP_ON && XNTP_CLIENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XNTPCLIENT_H */
