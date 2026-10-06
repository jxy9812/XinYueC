/**
 * @file       XGuiRemoteAuth.h
 * @brief      XGuiRemote 访问口令认证契约(挑战应答凭据存储与策略;
 *             冻结头, 只声明 API 不含实现)。
 * @details    XGuiServer/XGuiClient 认证(XGuiRemote.md §3.8)的实现主体
 *             收敛在本模块(server/client 只留挂接, 降低合并冲突面)。
 *             语义(2026-10-04 用户裁定, 见 XGuiRemote.md §3.8):
 *               - 服务器未设口令 = 匿名可连(默认行为, 完全向后兼容);
 *               - 服务器已设口令 = 新会话必须通过 SHA-256 挑战应答认证
 *                 (nonce 每连接随机, 防重放; 口令明文禁止过网/入日志,
 *                 服务端仅存口令 SHA-256, 原文不留存);
 *               - 错口令拒绝, 单连接失败次数达上限断链(上限可配置);
 *               - 口令运行期可经公开 C API 设置/清除: 设置后新会话须
 *                 认证, 存量会话不断; 清除即回匿名模式;
 *               - 认证与 TLS 正交(开不开 TLS 口令语义不变; TLS 开时
 *                 挑战仍走, 防应用层裸奔)。
 *             与遗留 API(setAuthMethod/setPassword)的关系: 遗留 API
 *             语义不变(方法与口令双旋钮); 新 API(setAccessPassword 族)
 *             以口令为单旋钮——设口令即启用挑战, 清口令即回匿名。
 * @note       模块开关 XGUI_REMOTE_ON 见 XGuiRemoteProto.h。
 * @author     XinYueC 团队
 */
#ifndef XGUIREMOTEAUTH_H
#define XGUIREMOTEAUTH_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiRemoteProto.h"

#if XGUI_REMOTE_ON

/** @brief 认证失败处置判定(单连接内逐次应答的结论)。 */
typedef enum XGuiRemoteAuthVerdict {
    XGUI_REMOTE_AUTH_VERDICT_OK     = 0, /**< 应答正确: 准入。 */
    XGUI_REMOTE_AUTH_VERDICT_RETRY  = 1, /**< 应答错误但未达失败上限:
                                              可留在 AUTHENTICATING 再答。 */
    XGUI_REMOTE_AUTH_VERDICT_REJECT = 2  /**< 应答错误且已达失败上限:
                                              断链(BYE AUTH_FAILED)。 */
} XGuiRemoteAuthVerdict;

/**
 * @brief 服务端认证凭据与失败策略存储(嵌入 XGuiServer 私有块;
 *        全部字段仅 GUI 线程访问)。
 */
typedef struct XGuiRemoteAuthServerStore {
    XGuiRemoteAuthMethod authMethod;   /**< 认证方法旋钮(默认 NONE; 遗留
                                         API setAuthMethod 可见可改)。 */
    uint8_t passwordHash[XGUI_REMOTE_AUTH_RESPONSE_BYTES];
                                       /**< 口令 SHA-256(原文即刻哈希,
                                         不留存; XGuiRemote.md §6.8)。 */
    bool    hasPassword;               /**< 已设口令。 */
    int     failureLimit;              /**< 单连接认证失败断链上限
                                         (默认 1=错 1 次即断; ≥1 有效)。 */
} XGuiRemoteAuthServerStore;

/* ==================== 服务端存储操作(挂接 XGuiServer 私有块) ========= */

/** @brief 初始化存储(零化+默认值; 默认=匿名可连, 向后兼容基线)。 */
void XGuiRemoteAuth_storeInit(XGuiRemoteAuthServerStore* store);

/**
 * @brief  遗留语义设口令: 仅写凭据, 不动方法旋钮(方法仍由
 *         setAuthMethod 决定; §6.8 SHA256+无口令 listen 拒绝契约不变)。
 * @return true 已设置; false 参数非法/哈希失败。空串等价清除(返回 true)。
 */
bool XGuiRemoteAuth_storeSetPassword(XGuiRemoteAuthServerStore* store,
                                     const char* passwordUtf8);

/** @brief 遗留语义清口令: 零化凭据哈希(防残留在栈/堆)。 */
void XGuiRemoteAuth_storeClearPassword(XGuiRemoteAuthServerStore* store);

/** @brief 是否已设口令。 */
bool XGuiRemoteAuth_storeHasPassword(const XGuiRemoteAuthServerStore* store);

/* ==================== 访问口令新语义(口令单旋钮, 用户裁定) =========== */

/**
 * @brief  设置访问口令并启用挑战应答(运行期公开 API 语义):
 *         即刻哈希入存 + 方法旋钮置 SHA256_CHALLENGE。对已有会话无影响;
 *         此后接入的新会话必须通过认证。空串等价 clearAccessPassword。
 * @return true 已设置; false 参数非法/哈希失败。
 */
bool XGuiRemoteAuth_serverSetAccessPassword(XGuiRemoteAuthServerStore* store,
                                            const char* passwordUtf8);

/** @brief 清除访问口令并回匿名模式(方法旋钮回 NONE); 已有会话不断。 */
void XGuiRemoteAuth_serverClearAccessPassword(
    XGuiRemoteAuthServerStore* store);

/**
 * @brief      查询访问口令状态(掩码形态, 不明文回吐)。
 * @param      maskedOutUtf8 掩码输出缓冲(可为 NULL=仅查询是否已设)。
 *             已设时写入定长掩码 "********"(8 星, 不泄露真实长度);
 *             未设时写入空串。
 * @param      cap 缓冲容量(maskedOutUtf8 非 NULL 时须 >0)。
 * @return     是否已设口令。
 */
bool XGuiRemoteAuth_serverAccessPassword(
    const XGuiRemoteAuthServerStore* store, char* maskedOutUtf8, size_t cap);

/** @brief 设置单连接认证失败断链上限(≥1 有效, 其余归一为 1)。 */
void XGuiRemoteAuth_serverSetFailureLimit(XGuiRemoteAuthServerStore* store,
                                          int maxFailures);

/** @brief 当前失败断链上限。 */
int XGuiRemoteAuth_serverFailureLimit(
    const XGuiRemoteAuthServerStore* store);

/**
 * @brief      校验 AUTH_RESPONSE 并推进失败计数(策略内核)。
 * @details    同式重算 expected = SHA256(storedHash || nonce) 并与应答
 *             常量时间比对; 错误时 ++(*failCountInOut) 并按上限判定
 *             RETRY/REJECT。未设口令直接 REJECT(不应发生: 未设口令的
 *             服务器不发出挑战)。
 * @param      failCountInOut 本连接累计失败计数(会话私有, 入口保证非 NULL)。
 */
XGuiRemoteAuthVerdict XGuiRemoteAuth_serverVerifyResponse(
    XGuiRemoteAuthServerStore* store,
    const uint8_t* nonce, size_t nonceBytes,
    const uint8_t* response, size_t responseBytes,
    int* failCountInOut);

/* ==================== 客户端凭据(挂接 XGuiClient 私有块) ============ */

/**
 * @brief  客户端设口令(明文驻留至认证完成即焚, 冻结头注)。
 * @param  buf       口令缓冲(调用方私有块内, 终止保证)。
 * @param  cap       缓冲容量。
 * @return true 已登记(超长截断); false 参数非法。
 */
bool XGuiRemoteAuth_clientSetPassword(char* buf, size_t cap,
                                      const char* passwordUtf8);

/** @brief 客户端清口令(零化缓冲, 防残留)。buf 可为 NULL(no-op)。 */
void XGuiRemoteAuth_clientClearPassword(char* buf, size_t cap);

/**
 * @brief  计算挑战应答: response = SHA256(SHA256(口令) || nonce)。
 * @return true 已算出; false 参数非法/哈希失败。
 */
bool XGuiRemoteAuth_clientComputeResponse(const char* passwordUtf8,
                                          const uint8_t* nonce,
                                          size_t nonceBytes,
                                          uint8_t outResponse[
                                              XGUI_REMOTE_AUTH_RESPONSE_BYTES]);

#endif /* XGUI_REMOTE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUIREMOTEAUTH_H */
