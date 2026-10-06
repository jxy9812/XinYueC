/**@****************************************************************************
 * @file       XGuiRemoteAuth.c
 * @brief      XGuiRemote 访问口令认证实现(挑战应答凭据存储与策略内核)。
 * @details    XGuiServer/XGuiClient 认证的实现主体(XGuiRemoteAuth.h 契约):
 *               - 口令即刻哈希存储(SHA-256, 仓内 XCryptographicHash),
 *                 原文不留存; 清除时零化防残留;
 *               - 挑战应答同式重算: response = SHA256(storedHash || nonce),
 *                 比对走常量时间路径(不因字节差异提前返回);
 *               - 访问口令单旋钮语义(2026-10-04 用户裁定): 设口令即启用
 *                 SHA256_CHALLENGE, 清口令即回匿名; 只影响此后接入的新
 *                 会话, 存量会话不断;
 *               - 单连接失败上限策略: 错 1 次即断(默认, 与历史口径一致)
 *                 或配置为 N 次内可重试、第 N 次失败断链。
 *             本文件不打印任何口令相关内容(红线: 口令明文禁止入日志);
 *             掩码查询输出定长 "********", 不泄露真实长度。
 * @note       模块开关 XGUI_REMOTE_ON 见 XGuiRemoteProto.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGuiRemoteAuth.h"

#if XGUI_REMOTE_ON

#include "XMemory.h"
#include "XClass.h"
#include "XByteArray.h"
#include "XCryptographicHash.h"
#include <string.h>

/* ==================== 内部小工具 ==================== */

/** @brief 口令掩码(定长, 不泄露真实长度)。 */
#define XGRA_PASSWORD_MASK "********"

/**
 * @brief 常量时间比较(长度恒定为响应字节数; 不因差异提前返回)。
 * @return true 全等。
 */
static bool xgra_constTimeEqual(const uint8_t* a, const uint8_t* b,
                                size_t n)
{
    uint8_t diff = 0;
    size_t i;
    for (i = 0; i < n; ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

/** @brief 单连接失败断链上限归一(≥1 有效, 其余归 1)。 */
static int xgra_clampLimit(int limit)
{
    return limit >= 1 ? limit : 1;
}

/* ==================== 服务端存储操作 ==================== */

void XGuiRemoteAuth_storeInit(XGuiRemoteAuthServerStore* store)
{
    if (!store) return;
    XMemset(store, 0, sizeof(*store));
    store->authMethod = XGUI_REMOTE_AUTH_NONE;
    store->failureLimit = 1; /* 默认错 1 次即断(历史口径)。 */
}

bool XGuiRemoteAuth_storeSetPassword(XGuiRemoteAuthServerStore* store,
                                     const char* passwordUtf8)
{
    XByteArray* digest;
    const uint8_t* data;
    int64_t size;
    size_t len;
    if (!store || !passwordUtf8) return false;
    len = strlen(passwordUtf8);
    if (len == 0) {
        XGuiRemoteAuth_storeClearPassword(store); /* 空串等价清除。 */
        return true;
    }
    /* 内部即刻哈希, 原文不留存(XGuiRemote.md §6.8)。 */
    digest = XCryptographicHash_hash(passwordUtf8, len,
                                     XCryptographicHash_Sha256);
    if (!digest) return false;
    data = XByteArray_constData(digest);
    size = (int64_t)XByteArray_size_base(digest);
    if (!data || size != (int64_t)XGUI_REMOTE_AUTH_RESPONSE_BYTES) {
        XClassDelete(digest);
        return false;
    }
    memcpy(store->passwordHash, data, XGUI_REMOTE_AUTH_RESPONSE_BYTES);
    XClassDelete(digest);
    store->hasPassword = true;
    return true;
}

void XGuiRemoteAuth_storeClearPassword(XGuiRemoteAuthServerStore* store)
{
    if (!store) return;
    XMemset(store->passwordHash, 0, sizeof(store->passwordHash));
    store->hasPassword = false;
}

bool XGuiRemoteAuth_storeHasPassword(const XGuiRemoteAuthServerStore* store)
{
    return store ? store->hasPassword : false;
}

/* ==================== 访问口令新语义(口令单旋钮) ==================== */

bool XGuiRemoteAuth_serverSetAccessPassword(XGuiRemoteAuthServerStore* store,
                                            const char* passwordUtf8)
{
    if (!store || !passwordUtf8) return false;
    if (passwordUtf8[0] == '\0') {
        XGuiRemoteAuth_serverClearAccessPassword(store);
        return true;
    }
    if (!XGuiRemoteAuth_storeSetPassword(store, passwordUtf8)) return false;
    /* 用户裁定语义: 设口令即启用挑战应答(新会话须认证, 存量不断)。 */
    store->authMethod = XGUI_REMOTE_AUTH_SHA256_CHALLENGE;
    return true;
}

void XGuiRemoteAuth_serverClearAccessPassword(
    XGuiRemoteAuthServerStore* store)
{
    if (!store) return;
    XGuiRemoteAuth_storeClearPassword(store);
    /* 用户裁定语义: 清口令即回匿名模式。 */
    store->authMethod = XGUI_REMOTE_AUTH_NONE;
}

bool XGuiRemoteAuth_serverAccessPassword(
    const XGuiRemoteAuthServerStore* store, char* maskedOutUtf8, size_t cap)
{
    bool has = store ? store->hasPassword : false;
    if (!maskedOutUtf8 || cap == 0) return has;
    if (has) {
        size_t n = sizeof(XGRA_PASSWORD_MASK) - 1;
        if (n > cap - 1) n = cap - 1;
        memcpy(maskedOutUtf8, XGRA_PASSWORD_MASK, n);
        maskedOutUtf8[n] = '\0';
    }
    else {
        maskedOutUtf8[0] = '\0';
    }
    return has;
}

void XGuiRemoteAuth_serverSetFailureLimit(XGuiRemoteAuthServerStore* store,
                                          int maxFailures)
{
    if (!store) return;
    store->failureLimit = xgra_clampLimit(maxFailures);
}

int XGuiRemoteAuth_serverFailureLimit(const XGuiRemoteAuthServerStore* store)
{
    return store ? xgra_clampLimit(store->failureLimit) : 1;
}

XGuiRemoteAuthVerdict XGuiRemoteAuth_serverVerifyResponse(
    XGuiRemoteAuthServerStore* store,
    const uint8_t* nonce, size_t nonceBytes,
    const uint8_t* response, size_t responseBytes,
    int* failCountInOut)
{
    XByteArrayView parts[2];
    uint8_t expected[XGUI_REMOTE_AUTH_RESPONSE_BYTES];
    XByteArrayView view;
    if (!store || !store->hasPassword || !nonce || !response ||
        !failCountInOut ||
        nonceBytes == 0 || nonceBytes > XGUI_REMOTE_AUTH_NONCE_BYTES ||
        responseBytes != XGUI_REMOTE_AUTH_RESPONSE_BYTES) {
        return XGUI_REMOTE_AUTH_VERDICT_REJECT;
    }
    /* 同式重算: expected = SHA256(storedHash || nonce)(§3.8)。 */
    parts[0].m_data = store->passwordHash;
    parts[0].m_size = (int64_t)XGUI_REMOTE_AUTH_RESPONSE_BYTES;
    parts[1].m_data = nonce;
    parts[1].m_size = (int64_t)nonceBytes;
    XMemset(expected, 0, sizeof(expected));
    view = XCryptographicHash_hashInto_1((char*)expected, sizeof(expected),
                                         parts, 2, XCryptographicHash_Sha256);
    if (view.m_size != (int64_t)sizeof(expected) ||
        !xgra_constTimeEqual(expected, response,
                             XGUI_REMOTE_AUTH_RESPONSE_BYTES)) {
        ++(*failCountInOut);
        return (*failCountInOut >= xgra_clampLimit(store->failureLimit))
                   ? XGUI_REMOTE_AUTH_VERDICT_REJECT
                   : XGUI_REMOTE_AUTH_VERDICT_RETRY;
    }
    return XGUI_REMOTE_AUTH_VERDICT_OK;
}

/* ==================== 客户端凭据 ==================== */

bool XGuiRemoteAuth_clientSetPassword(char* buf, size_t cap,
                                      const char* passwordUtf8)
{
    size_t n;
    if (!buf || cap == 0) return false;
    XMemset(buf, 0, cap); /* 换设先零化旧值。 */
    if (!passwordUtf8) return false;
    n = strlen(passwordUtf8);
    if (n >= cap) n = cap - 1;
    memcpy(buf, passwordUtf8, n);
    return true;
}

void XGuiRemoteAuth_clientClearPassword(char* buf, size_t cap)
{
    if (!buf || cap == 0) return;
    XMemset(buf, 0, cap);
}

bool XGuiRemoteAuth_clientComputeResponse(const char* passwordUtf8,
                                          const uint8_t* nonce,
                                          size_t nonceBytes,
                                          uint8_t outResponse[
                                              XGUI_REMOTE_AUTH_RESPONSE_BYTES])
{
    uint8_t storedHash[XGUI_REMOTE_AUTH_RESPONSE_BYTES];
    uint8_t concat[XGUI_REMOTE_AUTH_RESPONSE_BYTES +
                   XGUI_REMOTE_AUTH_NONCE_BYTES];
    XByteArrayView view;
    if (!passwordUtf8 || !nonce || !outResponse ||
        nonceBytes == 0 || nonceBytes > XGUI_REMOTE_AUTH_NONCE_BYTES) {
        return false;
    }
    /* 第一轮: storedHash = SHA256(password)。 */
    view = XCryptographicHash_hashInto((char*)storedHash, sizeof(storedHash),
                                       passwordUtf8, strlen(passwordUtf8),
                                       XCryptographicHash_Sha256);
    if (view.m_size != (int64_t)sizeof(storedHash)) return false;
    memcpy(concat, storedHash, sizeof(storedHash));
    memcpy(concat + sizeof(storedHash), nonce, nonceBytes);
    /* 第二轮: response = SHA256(storedHash || nonce)。 */
    view = XCryptographicHash_hashInto((char*)outResponse,
                                       XGUI_REMOTE_AUTH_RESPONSE_BYTES,
                                       (const char*)concat,
                                       sizeof(storedHash) + nonceBytes,
                                       XCryptographicHash_Sha256);
    return view.m_size == (int64_t)XGUI_REMOTE_AUTH_RESPONSE_BYTES;
}

#endif /* XGUI_REMOTE_ON */
