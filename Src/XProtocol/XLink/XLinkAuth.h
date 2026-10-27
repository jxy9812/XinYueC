/** @file       XLinkAuth.h
 * @brief      XLink 登录认证扩展：载荷编解码 + 口令派生 + 挑战应答校验。
 * @details    本模块是 XLinkSession 认证内核的**无状态**支撑层（XLINK_AUTH_ON）：
 *               - 四个认证会话帧（AuthRequest/AuthChallenge/AuthResponse/
 *                 AuthResult）的编解码（纯函数，无堆分配，逐字段小端序列化）；
 *               - 口令派生 H = iteratedHMAC-SHA256(password, salt, iterations)
 *                 与挑战应答 response = HMAC-SHA256(H, nonce)、
 *                 服务端证明 serverProof = HMAC-SHA256(H, nonce || 0x01)；
 *               - 常量时间比较（防时序侧信道）。
 *
 *             安全口径（与 XGuiRemoteAuth 对齐并加强）：
 *               - 口令明文禁止过网、禁止入日志；服务端只持有派生摘要 H 与盐；
 *               - nonce 每连接随机（XRandomGenerator_fillSecure），防重放；
 *               - 挑战应答**不防离线字典攻击**（盐与响应在线上可见）——跨网段
 *                 必须叠加 TLS（由传输层 XSslSocket 承担，协议层不实现密码学）；
 *               - 服务端证明让客户端能识别假冒服务端（双向认证的最低限度）。
 *
 *             兼容性：所有帧均为会话子码追加，老对端 default 分支静默忽略；
 *             能力位 XLinkCapability_Auth 未协商时本模块不参与线上交互。
 * @author     XinYueC 团队
 */
#ifndef XLINKAUTH_H
#define XLINKAUTH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XLink_config.h"

#if XLINK_ON && XLINK_AUTH_ON

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "XLinkProto.h"

/* ========================================================================== */
/*                        载荷编解码（纯函数）                               */
/* ========================================================================== */

/* 线上布局（全部小端，禁止 memcpy 结构体上线）：
 *   AuthRequest   : [u8 nameLen][name...]
 *   AuthChallenge : [u8 method][u8 saltLen][u16 reserved][u32 iterations]
 *                   [salt 16][nonce 32]
 *   AuthResponse  : [u8 method][u8 reserved][u16 len][response 32]
 *   AuthResult    : [u8 status][u8 role][u16 textLen][text...][serverProof 32]
 */

/**
 * @brief      编码 AuthRequest 载荷。
 * @param      msg 消息（借用）；user 长度须 < XLINK_AUTH_NAME_BYTES。
 * @param      buf 输出缓冲（调用方提供）；不得为 NULL。
 * @param      capacity 输出缓冲容量（字节）。
 * @param      outLen 实际写入字节数（可为 NULL）。
 * @return     成功返回 true；参数非法或容量不足（不产生半写）返回 false。
 */
bool XLinkAuth_encodeRequest(const XLinkAuthRequestPayload* msg, uint8_t* buf,
                             size_t capacity, size_t* outLen);

/**
 * @brief      解码 AuthRequest 载荷。
 * @param      buf 输入缓冲（借用）。
 * @param      length 输入字节数。
 * @param      out 输出消息（调用方提供存储）。
 * @return     成功返回 true；畸形（长度为 0/超上限/越界）返回 false。
 * @note       用户名保证非终止符（out->m_name 后不写 '\0'），调用方按
 *             m_nameLen 取用；为方便 C 用法，out->m_name[m_nameLen] 位置
 *             恒写 '\0'（在缓冲容量内）。
 */
bool XLinkAuth_decodeRequest(const uint8_t* buf, size_t length,
                             XLinkAuthRequestPayload* out);

/**
 * @brief      编码 AuthChallenge 载荷。
 * @return     成功返回 true；容量不足返回 false。
 */
bool XLinkAuth_encodeChallenge(const XLinkAuthChallengePayload* msg,
                               uint8_t* buf, size_t capacity, size_t* outLen);

/**
 * @brief      解码 AuthChallenge 载荷。
 * @return     成功返回 true；方法未知/盐长度不符/迭代次数为 0/越界返回 false。
 */
bool XLinkAuth_decodeChallenge(const uint8_t* buf, size_t length,
                               XLinkAuthChallengePayload* out);

/**
 * @brief      编码 AuthResponse 载荷。
 * @return     成功返回 true；容量不足返回 false。
 */
bool XLinkAuth_encodeResponse(const XLinkAuthResponsePayload* msg, uint8_t* buf,
                              size_t capacity, size_t* outLen);

/**
 * @brief      解码 AuthResponse 载荷。
 * @return     成功返回 true；长度不符/越界返回 false。
 */
bool XLinkAuth_decodeResponse(const uint8_t* buf, size_t length,
                              XLinkAuthResponsePayload* out);

/**
 * @brief      编码 AuthResult 载荷。
 * @param      msg 消息（借用）；文本超长按 XLINK_AUTH_TEXT_BYTES 截断。
 * @return     成功返回 true；容量不足返回 false。
 */
bool XLinkAuth_encodeResult(const XLinkAuthResultPayload* msg, uint8_t* buf,
                            size_t capacity, size_t* outLen);

/**
 * @brief      解码 AuthResult 载荷。
 * @return     成功返回 true；越界返回 false。
 */
bool XLinkAuth_decodeResult(const uint8_t* buf, size_t length,
                            XLinkAuthResultPayload* out);

/* ========================================================================== */
/*                        口令派生与挑战应答                                 */
/* ========================================================================== */

/**
 * @brief      计算口令派生摘要 H = iteratedHMAC-SHA256(password, salt)。
 * @param      password 口令明文（借用；不保存、不打印）。
 * @param      salt 盐（借用，XLINK_AUTH_SALT_BYTES 字节）。
 * @param      iterations 迭代次数（>= 1）。
 * @param      outHash 输出 H（XLINK_AUTH_NONCE_BYTES 字节）。
 * @return     成功返回 true；参数非法或密码学库失败返回 false。
 * @note       算法与 XConsoleShellLogin 的账户库一致（随机盐 + 迭代 HMAC），
 *             便于账户库互通/迁移。空口令视为非法。
 */
bool XLinkAuth_deriveHash(const char* password,
                          const uint8_t salt[XLINK_AUTH_SALT_BYTES],
                          uint32_t iterations,
                          uint8_t outHash[XLINK_AUTH_NONCE_BYTES]);

/**
 * @brief      计算挑战应答 response = HMAC-SHA256(H, nonce)。
 * @param      hash H（XLINK_AUTH_NONCE_BYTES 字节）。
 * @param      nonce 挑战（XLINK_AUTH_NONCE_BYTES 字节）。
 * @param      outResponse 输出应答（XLINK_AUTH_NONCE_BYTES 字节）。
 * @return     成功返回 true。
 */
bool XLinkAuth_computeResponse(
    const uint8_t hash[XLINK_AUTH_NONCE_BYTES],
    const uint8_t nonce[XLINK_AUTH_NONCE_BYTES],
    uint8_t outResponse[XLINK_AUTH_NONCE_BYTES]);

/**
 * @brief      计算服务端证明 serverProof = HMAC-SHA256(H, nonce || 0x01)。
 * @param      hash H（XLINK_AUTH_NONCE_BYTES 字节）。
 * @param      nonce 挑战（XLINK_AUTH_NONCE_BYTES 字节）。
 * @param      outProof 输出证明（XLINK_AUTH_NONCE_BYTES 字节）。
 * @return     成功返回 true。
 * @note       客户端据此确认对端确实持有 H（而非仅仅回了个"成功"）。
 *             客户端应答与服务端证明使用不同输入（nonce vs nonce||0x01），
 *             互相不可替代，避免应答被直接回放当作证明。
 */
bool XLinkAuth_computeServerProof(
    const uint8_t hash[XLINK_AUTH_NONCE_BYTES],
    const uint8_t nonce[XLINK_AUTH_NONCE_BYTES],
    uint8_t outProof[XLINK_AUTH_NONCE_BYTES]);

/**
 * @brief      常量时间比较（长度固定，不因差异提前返回）。
 * @return     全等返回 true。
 * @note       用于比较应答与证明，避免按字节比较泄漏时序信息。
 */
bool XLinkAuth_constantTimeEqual(const uint8_t* a, const uint8_t* b,
                                 size_t length);

/**
 * @brief      生成密码学安全随机字节（盐/nonce）。
 * @param      buf 输出缓冲（调用方提供）。
 * @param      length 字节数。
 * @return     成功返回 true。
 */
bool XLinkAuth_randomFill(uint8_t* buf, size_t length);

/**
 * @brief      零化敏感缓冲（防编译器优化掉），用于口令/H/应答清理。
 * @param      data 缓冲（可为 NULL）。
 * @param      length 字节数。
 */
void XLinkAuth_secureZero(void* data, size_t length);

#endif /* XLINK_ON && XLINK_AUTH_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKAUTH_H */
