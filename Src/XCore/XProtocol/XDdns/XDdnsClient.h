/** @file       XDdnsClient.h
 * @brief      XDdns 客户端——公网 IP 探测 + DNS 记录比对更新的编排公开 API。
 * @details    工作流：checkNow 触发一轮「探测公网 IP（或手动注入）→ 与
 *             最近一次推送值比对 → 查询云厂商记录 → 增/改/跳过」，结果经
 *             信号通知。厂商通过 XDdnsProvider 抽象层接入（首个实现为
 *             阿里云）。HTTP/HTTPS 由 XNetworkAccessManager 承载。
 *             实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX 或其他平台 API。
 *
 * @note       单线程事件循环口径；setServer 类配置含 DNS 解析/XHttp 准备，
 *             请在启动阶段完成。探测接口响应较慢时 checkNow 内部会自旋
 *             事件循环（XUpgrade 同款 deadline 口径），期间同线程其余任务
 *             暂停——与所有同步等待型封装一致。
 */
#ifndef XDDNSCLIENT_H
#define XDDNSCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XDDNS_ON / XDDNS_CLIENT_ON 已定义
   （必须早于下方条件编译判定，否则直接 include 本头文件时开关未定义、
   整段声明会被跳过） */
#include "CXinYueConfig.h"
#include "XDdns_config.h"

#if XDDNS_ON && XDDNS_CLIENT_ON

#include "XObject.h"
#include "XString.h"
#include "XDdnsProto.h"
#include "XDdnsProvider.h"

/* 私有状态前置声明（定义在 XDdnsClient_Protected.h，仅供实现使用）。 */
typedef struct XDdnsClientPrivate XDdnsClientPrivate;

/* ========================================================================== */
/*                        虚函数表                                           */
/* ========================================================================== */

XCLASS_DEFINE_BEGING(XDdnsClient)
XCLASS_DEFINE_ENUM(XDdnsClient, TimerEvent) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XDdnsClient)

/* ========================================================================== */
/*                        对象                                               */
/* ========================================================================== */

/**
 * @brief XDdns 客户端对象。
 * @note  m_class 为第一个成员、由 XClass 管理、禁止手工修改。
 */
typedef struct XDdnsClient {
    XObject m_class;        /**< 基类；第一个成员、由 XClass 管理、禁止手工修改。 */
    XDdnsClientPrivate* m_d; /**< 私有状态（对象拥有）；仅供实现使用。 */
} XDdnsClient;

XVtable* XDdnsClient_class_init(void);

/**
 * @brief      栈上初始化客户端对象。
 * @param      obj 待初始化对象（调用方提供存储空间）；NULL 不执行任何操作。
 */
void XDdnsClient_init(XDdnsClient* obj);

/**
 * @brief      创建堆客户端对象。
 * @return     成功返回新对象指针；分配失败返回 NULL。须用 XClassDelete 释放。
 */
XDdnsClient* XDdnsClient_create(void);

/* ========================================================================== */
/*                        配置                                               */
/* ========================================================================== */

/**
 * @brief      选择 DNS 服务商。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      name 厂商名（XDdnsProvider_find 的键；"aliyun"）。
 * @return     成功返回 true；厂商未注册返回 false（保持原厂商）。
 */
bool XDdnsClient_setProvider(XDdnsClient* self, const char* name);

/**
 * @brief      覆盖厂商 API 端点（默认官方端点）。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      urlUtf8 端点 URL（含 scheme；空串恢复默认）。
 * @return     成功返回 true。
 * @note       供测试指向本地 mock、专有云指向私有端点。
 */
bool XDdnsClient_setEndpoint(XDdnsClient* self, const char* urlUtf8);

/**
 * @brief      配置云厂商访问凭据（XString 版）。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      keyId AccessKey ID（拷贝语义）。
 * @param      keySecret AccessKey Secret（拷贝语义，仅参与本地签名）。
 * @return     成功返回 true。
 */
bool XDdnsClient_setCredentials(XDdnsClient* self, const XString* keyId,
                                const XString* keySecret);

/**
 * @brief      配置云厂商访问凭据（UTF-8 便捷版）。
 */
bool XDdnsClient_setCredentials_2(XDdnsClient* self, const char* keyId,
                                  const char* keySecret);

/**
 * @brief      配置要维护的 DNS 记录（XString 版）。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      domain 主域名（如 "example.com"）。
 * @param      rr 主机记录（如 "www"；"@" 表示根域）。
 * @param      type 记录类型（A/AAAA）。
 * @return     成功返回 true。
 */
bool XDdnsClient_setRecord(XDdnsClient* self, const XString* domain,
                           const XString* rr, XDdnsRecordType type);

/**
 * @brief      配置要维护的 DNS 记录（UTF-8 便捷版）。
 */
bool XDdnsClient_setRecord_2(XDdnsClient* self, const char* domain,
                             const char* rr, XDdnsRecordType type);

/**
 * @brief      设置公网 IP 探测接口（覆盖默认列表中的某一项）。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      urlUtf8 探测 URL（应答体须为纯 IP 文本）；NULL 清除该项。
 * @param      index 槽位（0..XDDNS_MAX_PROBE_URLS-1）。
 * @return     成功返回 true。
 * @note       探测时按槽位顺序尝试，首个成功者生效；默认列表见
 *             XDdns_config.h（按记录类型自动选择 v4/v6 列表）。
 */
bool XDdnsClient_setProbeUrl_2(XDdnsClient* self, const char* urlUtf8,
                               uint32_t index);

/**
 * @brief      手动注入本机 IP（跳过 HTTP 探测）。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      ipUtf8 IP 字符串；NULL 清除手动模式回到自动探测。
 * @return     合法 IP 返回 true；非法返回 false（不改变原状态）。
 * @note       供路由器/外部脚本直接告知 IP，探测接口不可达时的兜底。
 */
bool XDdnsClient_setLocalIp_2(XDdnsClient* self, const char* ipUtf8);

/**
 * @brief      设置自动检查周期。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      intervalMs 周期毫秒；0 关闭自动检查。
 * @return     成功返回 true。
 * @note       检查内部有「IP 未变化不发 API 请求」的短路，短周期对云端
 *             无压力（仅消耗一次探测请求）。
 */
bool XDdnsClient_setCheckInterval(XDdnsClient* self, uint32_t intervalMs);

/**
 * @brief      设置 HTTPS 对端证书校验。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      on true 严格校验（需系统/自定义 CA 可信链），false 跳过
 *             （默认；嵌入式无 CA 库的现实选择，阿里云 API 本身有签名保护）。
 * @return     成功返回 true。
 */
bool XDdnsClient_setSslPeerVerify(XDdnsClient* self, bool on);

/**
 * @brief      设置单次 HTTP 请求超时。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @param      timeoutMs 超时毫秒；0 取默认 XDDNS_HTTP_TIMEOUT_MS。
 * @return     成功返回 true。
 */
bool XDdnsClient_setTimeout(XDdnsClient* self, uint32_t timeoutMs);

/* ========================================================================== */
/*                        检查                                               */
/* ========================================================================== */

/**
 * @brief      立即执行一轮检查（异步语义：探测与 API 调用期间自旋事件循环）。
 * @param      self 客户端实例；NULL 不执行任何操作。
 * @return     流程已启动返回 true；缺配置或上一轮未结束返回 false。
 * @note       结果经信号通知：recordUpdated / recordUpToDate / checkFailed。
 */
bool XDdnsClient_checkNow(XDdnsClient* self);

/* ========================================================================== */
/*                        查询                                               */
/* ========================================================================== */

/** @brief 查询状态；NULL 返回 Idle。 */
XDdnsClientState XDdnsClient_state(const XDdnsClient* self);
/** @brief 查询最近失败原因；NULL 返回 None。 */
XDdnsError XDdnsClient_lastError(const XDdnsClient* self);
/** @brief 最近一次 HTTP 应答状态码（诊断用；0=未收到应答）。
 *  @note 400/403 多为云端拒绝 POP 签名或权限（密钥/时钟偏差/授权范围）。 */
int XDdnsClient_lastHttpStatus(const XDdnsClient* self);
/** @brief 云端错误码（借用，空串=无）。如 SignatureDoesNotMatch /
 *  InvalidAccessKeyId.NotFound / Forbidden.RAM，直接指向失败根因。 */
const char* XDdnsClient_lastApiCode_const(const XDdnsClient* self);
/** @brief 最近一次探测/注入的 IP（借用，内部存储；NULL 从未成功）。 */
const XString* XDdnsClient_currentIp_const(const XDdnsClient* self);
/** @brief 最近一次成功写入 DNS 的 IP（借用；NULL 从未推送）。 */
const XString* XDdnsClient_pushedIp_const(const XDdnsClient* self);

/* ========================================================================== */
/*                        信号                                               */
/* ========================================================================== */

/**
 * @brief 公网 IP 已探测/注入信号。
 * @param self 客户端实例指针。
 * @param ip 本次探测到的 IP（XString*，深拷贝，接收方只读借用）。
 * @return 信号发送结果。
 */
void* XDdnsClient_ipDetected_signal(XDdnsClient* self, const XString* ip);

/**
 * @brief DNS 记录已更新信号（发生 Add 或 Update 时发出）。
 * @param self 客户端实例指针。
 * @param ip 写入记录的 IP（XString*，深拷贝）。
 * @return 信号发送结果。
 */
void* XDdnsClient_recordUpdated_signal(XDdnsClient* self, const XString* ip);

/**
 * @brief 记录已一致信号（值相同未发更新请求，或记录本就正确）。
 * @param self 客户端实例指针。
 * @param ip 当前 IP（XString*，深拷贝）。
 * @return 信号发送结果。
 */
void* XDdnsClient_recordUpToDate_signal(XDdnsClient* self, const XString* ip);

/**
 * @brief 检查失败信号（原因查 XDDNSClient_lastError）。
 * @param self 客户端实例指针。
 * @param error 失败原因（XDdnsError）。
 * @return 信号发送结果。
 */
void* XDdnsClient_checkFailed_signal(XDdnsClient* self, int error);

#endif /* XDDNS_ON && XDDNS_CLIENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDDNSCLIENT_H */
