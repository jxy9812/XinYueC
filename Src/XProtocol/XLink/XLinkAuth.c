/** @file       XLinkAuth.c
 * @brief      XLink 登录认证扩展实现：载荷编解码 + 口令派生 + 挑战应答。
 * @details    只依赖 XinYueC 抽象层（XCryptographicHash / XRandomGenerator /
 *             XMemory），禁止调用 Win32、POSIX 或其他平台 API。
 *             所有编解码均为纯函数，逐字段小端读写；解析侧不信任任何长度
 *             字段，逐字段核对剩余空间后才推进（防畸形载荷越界）。
 *             本文件不打印任何口令/摘要/nonce 内容（红线）。
 * @author     XinYueC 团队
 */
#include "XLinkAuth.h"

#if XLINK_ON && XLINK_AUTH_ON

#include "XCryptographicHash.h"
#include "XRandomGenerator.h"
#include "XMemory.h"
#include <string.h>

/* ========================================================================== */
/*                        内部常量与工具                                     */
/* ========================================================================== */

/** @brief AuthChallenge 固定头部字节数（method+saltLen+reserved+iterations）。 */
#define XLINK_AUTH_CHALLENGE_HEADER 8u

/** @brief AuthResponse 固定头部字节数（method+reserved+len）。 */
#define XLINK_AUTH_RESPONSE_HEADER 4u

/** @brief AuthResult 固定头部字节数（status+role+textLen）。 */
#define XLINK_AUTH_RESULT_HEADER 4u

/** @brief 服务端证明的域分离字节（nonce || 0x01）。 */
#define XLINK_AUTH_PROOF_DOMAIN 0x01u

/** @brief 口令派生中间缓冲（键与消息复用同一块，避免额外栈开销）。 */
typedef struct XLinkAuthDigestBuf {
    uint8_t bytes[XLINK_AUTH_NONCE_BYTES];
} XLinkAuthDigestBuf;

/* ========================================================================== */
/*                        载荷编解码                                         */
/* ========================================================================== */

bool XLinkAuth_encodeRequest(const XLinkAuthRequestPayload* msg, uint8_t* buf,
                             size_t capacity, size_t* outLen)
{
    size_t nameLen;
    if (!msg || !buf)
        return false;
    nameLen = msg->m_nameLen;
    if (nameLen == 0 || nameLen > XLINK_AUTH_NAME_BYTES)
        return false;
    if (capacity < 1u + nameLen)
        return false;
    buf[0] = (uint8_t)nameLen;
    memcpy(&buf[1], msg->m_name, nameLen);
    if (outLen)
        *outLen = 1u + nameLen;
    return true;
}

bool XLinkAuth_decodeRequest(const uint8_t* buf, size_t length,
                             XLinkAuthRequestPayload* out)
{
    uint8_t nameLen;
    if (!buf || !out || length < 1)
        return false;
    nameLen = buf[0];
    if (nameLen == 0 || nameLen > XLINK_AUTH_NAME_BYTES)
        return false;
    if (length < 1u + (size_t)nameLen)
        return false;
    memset(out, 0, sizeof(*out));
    out->m_nameLen = nameLen;
    memcpy(out->m_name, &buf[1], nameLen);
    /* 便于 C 侧按字符串取用；m_nameLen 才是权威长度。 */
    if (nameLen < XLINK_AUTH_NAME_BYTES)
        out->m_name[nameLen] = '\0';
    return true;
}

bool XLinkAuth_encodeChallenge(const XLinkAuthChallengePayload* msg,
                               uint8_t* buf, size_t capacity, size_t* outLen)
{
    size_t total = XLINK_AUTH_CHALLENGE_HEADER
                   + XLINK_AUTH_SALT_BYTES + XLINK_AUTH_NONCE_BYTES;
    if (!msg || !buf || capacity < total)
        return false;
    buf[0] = msg->m_method;
    buf[1] = XLINK_AUTH_SALT_BYTES;
    buf[2] = 0;
    buf[3] = 0;
    XMemory_write_data(&buf[4], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&msg->m_iterations,
                       sizeof(msg->m_iterations));
    memcpy(&buf[8], msg->m_salt, XLINK_AUTH_SALT_BYTES);
    memcpy(&buf[8 + XLINK_AUTH_SALT_BYTES], msg->m_nonce,
           XLINK_AUTH_NONCE_BYTES);
    if (outLen)
        *outLen = total;
    return true;
}

bool XLinkAuth_decodeChallenge(const uint8_t* buf, size_t length,
                               XLinkAuthChallengePayload* out)
{
    size_t need = XLINK_AUTH_CHALLENGE_HEADER
                  + XLINK_AUTH_SALT_BYTES + XLINK_AUTH_NONCE_BYTES;
    uint32_t iterations = 0;
    if (!buf || !out || length < need)
        return false;
    if (buf[1] != XLINK_AUTH_SALT_BYTES)
        return false;
    if (buf[0] != (uint8_t)XLinkAuthMethod_IteratedHmac)
        return false;
    XMemory_read_data(&buf[4], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&iterations, sizeof(iterations));
    if (iterations == 0)
        return false;
    memset(out, 0, sizeof(*out));
    out->m_method = buf[0];
    out->m_saltLen = buf[1];
    out->m_reserved = 0;
    out->m_iterations = iterations;
    memcpy(out->m_salt, &buf[8], XLINK_AUTH_SALT_BYTES);
    memcpy(out->m_nonce, &buf[8 + XLINK_AUTH_SALT_BYTES],
           XLINK_AUTH_NONCE_BYTES);
    return true;
}

bool XLinkAuth_encodeResponse(const XLinkAuthResponsePayload* msg, uint8_t* buf,
                              size_t capacity, size_t* outLen)
{
    uint16_t respLen = (uint16_t)XLINK_AUTH_NONCE_BYTES;
    size_t total = XLINK_AUTH_RESPONSE_HEADER + XLINK_AUTH_NONCE_BYTES;
    if (!msg || !buf || capacity < total)
        return false;
    buf[0] = msg->m_method;
    buf[1] = 0;
    XMemory_write_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&respLen, sizeof(respLen));
    memcpy(&buf[4], msg->m_response, XLINK_AUTH_NONCE_BYTES);
    if (outLen)
        *outLen = total;
    return true;
}

bool XLinkAuth_decodeResponse(const uint8_t* buf, size_t length,
                              XLinkAuthResponsePayload* out)
{
    uint16_t respLen = 0;
    if (!buf || !out || length < XLINK_AUTH_RESPONSE_HEADER)
        return false;
    XMemory_read_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&respLen, sizeof(respLen));
    if (respLen != XLINK_AUTH_NONCE_BYTES)
        return false;
    if (length < XLINK_AUTH_RESPONSE_HEADER + (size_t)respLen)
        return false;
    memset(out, 0, sizeof(*out));
    out->m_method = buf[0];
    out->m_len = respLen;
    memcpy(out->m_response, &buf[XLINK_AUTH_RESPONSE_HEADER], respLen);
    return true;
}

bool XLinkAuth_encodeResult(const XLinkAuthResultPayload* msg, uint8_t* buf,
                            size_t capacity, size_t* outLen)
{
    size_t textLen;
    size_t total;
    uint16_t textLenU16;
    if (!msg || !buf)
        return false;
    textLen = msg->m_textLen;
    if (textLen > XLINK_AUTH_TEXT_BYTES)
        textLen = XLINK_AUTH_TEXT_BYTES;
    total = XLINK_AUTH_RESULT_HEADER + textLen + XLINK_AUTH_NONCE_BYTES;
    if (capacity < total)
        return false;
    buf[0] = msg->m_status;
    buf[1] = msg->m_role;
    textLenU16 = (uint16_t)textLen;
    XMemory_write_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&textLenU16, sizeof(textLenU16));
    if (textLen > 0)
        memcpy(&buf[XLINK_AUTH_RESULT_HEADER], msg->m_text, textLen);
    memcpy(&buf[XLINK_AUTH_RESULT_HEADER + textLen], msg->m_serverProof,
           XLINK_AUTH_NONCE_BYTES);
    if (outLen)
        *outLen = total;
    return true;
}

bool XLinkAuth_decodeResult(const uint8_t* buf, size_t length,
                            XLinkAuthResultPayload* out)
{
    uint16_t textLen = 0;
    size_t need;
    if (!buf || !out || length < XLINK_AUTH_RESULT_HEADER)
        return false;
    XMemory_read_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&textLen, sizeof(textLen));
    if (textLen > XLINK_AUTH_TEXT_BYTES)
        return false;
    need = XLINK_AUTH_RESULT_HEADER + (size_t)textLen + XLINK_AUTH_NONCE_BYTES;
    if (length < need)
        return false;
    memset(out, 0, sizeof(*out));
    out->m_status = buf[0];
    out->m_role = buf[1];
    out->m_textLen = textLen;
    if (textLen > 0) {
        memcpy(out->m_text, &buf[XLINK_AUTH_RESULT_HEADER], textLen);
        if (textLen < XLINK_AUTH_TEXT_BYTES)
            out->m_text[textLen] = '\0';
    }
    memcpy(out->m_serverProof,
           &buf[XLINK_AUTH_RESULT_HEADER + textLen], XLINK_AUTH_NONCE_BYTES);
    return true;
}

/* ========================================================================== */
/*                        口令派生与挑战应答                                 */
/* ========================================================================== */

bool XLinkAuth_deriveHash(const char* password,
                          const uint8_t salt[XLINK_AUTH_SALT_BYTES],
                          uint32_t iterations,
                          uint8_t outHash[XLINK_AUTH_NONCE_BYTES])
{
    XLinkAuthDigestBuf previous;
    XLinkAuthDigestBuf current;
    XByteArrayView view;
    size_t passwordLen;
    uint32_t i;

    if (!password || !salt || !outHash || iterations == 0)
        return false;
    passwordLen = strlen(password);
    if (passwordLen == 0)
        return false;

    /* 第一轮：previous = HMAC(password, salt)。
       与 XConsoleShellLogin 的 xlogin_password_digest 同式，账户库可互通。 */
    view = XCryptographicHash_hmacInto(
        (char*)previous.bytes, sizeof(previous.bytes),
        password, passwordLen,
        (const char*)salt, XLINK_AUTH_SALT_BYTES,
        XCryptographicHash_Sha256);
    if (view.m_size != (int64_t)sizeof(previous.bytes)) {
        XLinkAuth_secureZero(&previous, sizeof(previous));
        return false;
    }

    /* 后续轮：current = HMAC(password, previous)，迭代到底。 */
    for (i = 1; i < iterations; ++i) {
        view = XCryptographicHash_hmacInto(
            (char*)current.bytes, sizeof(current.bytes),
            password, passwordLen,
            (const char*)previous.bytes, sizeof(previous.bytes),
            XCryptographicHash_Sha256);
        if (view.m_size != (int64_t)sizeof(current.bytes)) {
            XLinkAuth_secureZero(&previous, sizeof(previous));
            XLinkAuth_secureZero(&current, sizeof(current));
            return false;
        }
        memcpy(previous.bytes, current.bytes, sizeof(previous.bytes));
    }

    memcpy(outHash, previous.bytes, XLINK_AUTH_NONCE_BYTES);
    XLinkAuth_secureZero(&previous, sizeof(previous));
    XLinkAuth_secureZero(&current, sizeof(current));
    return true;
}

bool XLinkAuth_computeResponse(
    const uint8_t hash[XLINK_AUTH_NONCE_BYTES],
    const uint8_t nonce[XLINK_AUTH_NONCE_BYTES],
    uint8_t outResponse[XLINK_AUTH_NONCE_BYTES])
{
    XByteArrayView view;
    if (!hash || !nonce || !outResponse)
        return false;
    view = XCryptographicHash_hmacInto(
        (char*)outResponse, XLINK_AUTH_NONCE_BYTES,
        (const char*)hash, XLINK_AUTH_NONCE_BYTES,
        (const char*)nonce, XLINK_AUTH_NONCE_BYTES,
        XCryptographicHash_Sha256);
    return view.m_size == (int64_t)XLINK_AUTH_NONCE_BYTES;
}

bool XLinkAuth_computeServerProof(
    const uint8_t hash[XLINK_AUTH_NONCE_BYTES],
    const uint8_t nonce[XLINK_AUTH_NONCE_BYTES],
    uint8_t outProof[XLINK_AUTH_NONCE_BYTES])
{
    uint8_t message[XLINK_AUTH_NONCE_BYTES + 1u];
    XByteArrayView view;
    if (!hash || !nonce || !outProof)
        return false;
    memcpy(message, nonce, XLINK_AUTH_NONCE_BYTES);
    message[XLINK_AUTH_NONCE_BYTES] = XLINK_AUTH_PROOF_DOMAIN;
    view = XCryptographicHash_hmacInto(
        (char*)outProof, XLINK_AUTH_NONCE_BYTES,
        (const char*)hash, XLINK_AUTH_NONCE_BYTES,
        (const char*)message, sizeof(message),
        XCryptographicHash_Sha256);
    XLinkAuth_secureZero(message, sizeof(message));
    return view.m_size == (int64_t)XLINK_AUTH_NONCE_BYTES;
}

bool XLinkAuth_constantTimeEqual(const uint8_t* a, const uint8_t* b,
                                 size_t length)
{
    uint8_t diff = 0;
    size_t i;
    if (!a || !b)
        return false;
    for (i = 0; i < length; ++i)
        diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

bool XLinkAuth_randomFill(uint8_t* buf, size_t length)
{
    if (!buf || length == 0)
        return false;
    return XRandomGenerator_fillSecure(buf, length);
}

void XLinkAuth_secureZero(void* data, size_t length)
{
    volatile uint8_t* bytes = (volatile uint8_t*)data;
    if (!bytes)
        return;
    while (length--)
        *bytes++ = 0;
}

#endif /* XLINK_ON && XLINK_AUTH_ON */
