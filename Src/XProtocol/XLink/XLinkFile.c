/** @file       XLinkFile.c
 * @brief      XLink 文件传输扩展：载荷编解码与文件名安全校验实现。
 * @details    只依赖 XinYueC 抽象层（XStringUtils），禁止平台 API。
 *             所有解析函数逐字段核对剩余长度后才推进，不信任线上长度字段。
 * @author     XinYueC 团队
 */
#include "XLinkFile.h"

#if XLINK_ON && XLINK_FILE_ON

#include "XStringUtils.h"
#include <string.h>

/** @brief DATA 载荷上限（偏移 + 业务数据）。 */
#define XLINK_FILE_DATA_HEADER XLINK_FILE_DATA_OFFSET_BYTES

/* ========================================================================== */
/*                        小端读写原语                                       */
/* ========================================================================== */

static void xf_putU16(uint8_t* buf, uint16_t v)
{
    buf[0] = (uint8_t)(v & 0xFFu);
    buf[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static uint16_t xf_getU16(const uint8_t* buf)
{
    return (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

static void xf_putU32(uint8_t* buf, uint32_t v)
{
    buf[0] = (uint8_t)(v & 0xFFu);
    buf[1] = (uint8_t)((v >> 8) & 0xFFu);
    buf[2] = (uint8_t)((v >> 16) & 0xFFu);
    buf[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t xf_getU32(const uint8_t* buf)
{
    return (uint32_t)buf[0]
           | ((uint32_t)buf[1] << 8)
           | ((uint32_t)buf[2] << 16)
           | ((uint32_t)buf[3] << 24);
}

static void xf_putU64(uint8_t* buf, uint64_t v)
{
    xf_putU32(&buf[0], (uint32_t)(v & 0xFFFFFFFFu));
    xf_putU32(&buf[4], (uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

static uint64_t xf_getU64(const uint8_t* buf)
{
    return (uint64_t)xf_getU32(&buf[0])
           | ((uint64_t)xf_getU32(&buf[4]) << 32);
}

/* ========================================================================== */
/*                        Open / OpenAck                                     */
/* ========================================================================== */

/**
 * @brief Open 载荷线上布局（小端，固定 28 字节头 + 文件名）：
 *   [0]  u8  mode
 *   [1]  u8  flags
 *   [2]  u16 nameLen
 *   [4]  u32 chunkBytes
 *   [8]  u32 windowBytes
 *   [12] u64 totalSize
 *   [20] u64 startOffset
 *   [28] name...
 */
bool XLinkFile_encodeOpen(const XLinkFileOpenPayload* msg, uint8_t* buf,
                          size_t capacity, size_t* outLen)
{
    size_t nameLen;
    size_t total;

    if (!msg || !buf)
        return false;
    if (msg->m_mode != XLinkFileMode_Upload
        && msg->m_mode != XLinkFileMode_Download)
        return false;
    nameLen = msg->m_nameLen;
    if (nameLen == 0 || nameLen >= XLINK_FILE_NAME_BYTES)
        return false;
    total = XLINK_FILE_OPEN_HEADER + nameLen;
    if (capacity < total)
        return false;

    buf[0] = msg->m_mode;
    buf[1] = msg->m_flags;
    xf_putU16(&buf[2], msg->m_nameLen);
    xf_putU32(&buf[4], msg->m_chunkBytes);
    xf_putU32(&buf[8], msg->m_windowBytes);
    xf_putU64(&buf[12], msg->m_totalSize);
    xf_putU64(&buf[20], msg->m_startOffset);
    memcpy(&buf[XLINK_FILE_OPEN_HEADER], msg->m_name, nameLen);
    if (outLen)
        *outLen = total;
    return true;
}

bool XLinkFile_decodeOpen(const uint8_t* buf, size_t length,
                          XLinkFileOpenPayload* out)
{
    uint16_t nameLen;

    if (!buf || !out || length < XLINK_FILE_OPEN_HEADER)
        return false;
    if (buf[0] != XLinkFileMode_Upload && buf[0] != XLinkFileMode_Download)
        return false;
    nameLen = xf_getU16(&buf[2]);
    if (nameLen == 0 || nameLen >= XLINK_FILE_NAME_BYTES)
        return false;
    if (length < XLINK_FILE_OPEN_HEADER + (size_t)nameLen)
        return false;

    memset(out, 0, sizeof(*out));
    out->m_mode = buf[0];
    out->m_flags = buf[1];
    out->m_nameLen = nameLen;
    out->m_chunkBytes = xf_getU32(&buf[4]);
    out->m_windowBytes = xf_getU32(&buf[8]);
    out->m_totalSize = xf_getU64(&buf[12]);
    out->m_startOffset = xf_getU64(&buf[20]);
    memcpy(out->m_name, &buf[XLINK_FILE_OPEN_HEADER], nameLen);
    out->m_name[nameLen] = '\0';

    /* 起始偏移不能超过文件总大小（否则后续 DATA 永远对不上）。
       注意 totalSize=0（空文件）时 startOffset 也必须为 0。 */
    if (out->m_startOffset > out->m_totalSize)
        return false;
    return true;
}

bool XLinkFile_encodeOpenAck(const XLinkFileOpenAckPayload* msg, uint8_t* buf,
                             size_t capacity, size_t* outLen)
{
    size_t textLen;
    size_t total;

    if (!msg || !buf)
        return false;
    textLen = msg->m_textLen;
    if (textLen > XLINK_FILE_TEXT_BYTES)
        textLen = XLINK_FILE_TEXT_BYTES;
    total = XLINK_FILE_OPEN_ACK_HEADER + textLen;
    if (capacity < total)
        return false;

    buf[0] = msg->m_accepted;
    buf[1] = msg->m_status;
    xf_putU16(&buf[2], (uint16_t)textLen);
    xf_putU32(&buf[4], msg->m_chunkBytes);
    xf_putU32(&buf[8], msg->m_windowBytes);
    xf_putU64(&buf[12], msg->m_startOffset);
    xf_putU64(&buf[20], msg->m_totalSize);
    if (textLen > 0)
        memcpy(&buf[XLINK_FILE_OPEN_ACK_HEADER], msg->m_text, textLen);
    if (outLen)
        *outLen = total;
    return true;
}

bool XLinkFile_decodeOpenAck(const uint8_t* buf, size_t length,
                             XLinkFileOpenAckPayload* out)
{
    uint16_t textLen;

    if (!buf || !out || length < XLINK_FILE_OPEN_ACK_HEADER)
        return false;
    textLen = xf_getU16(&buf[2]);
    if (textLen > XLINK_FILE_TEXT_BYTES)
        return false;
    if (length < XLINK_FILE_OPEN_ACK_HEADER + (size_t)textLen)
        return false;

    memset(out, 0, sizeof(*out));
    out->m_accepted = buf[0];
    out->m_status = buf[1];
    out->m_textLen = textLen;
    out->m_chunkBytes = xf_getU32(&buf[4]);
    out->m_windowBytes = xf_getU32(&buf[8]);
    out->m_startOffset = xf_getU64(&buf[12]);
    out->m_totalSize = xf_getU64(&buf[20]);
    if (textLen > 0)
        memcpy(out->m_text, &buf[XLINK_FILE_OPEN_ACK_HEADER], textLen);
    out->m_text[textLen < XLINK_FILE_TEXT_BYTES ? textLen
                                                : XLINK_FILE_TEXT_BYTES - 1u]
        = '\0';
    return true;
}

/* ========================================================================== */
/*                        Data / Ack                                         */
/* ========================================================================== */

bool XLinkFile_encodeData(uint64_t offset, const uint8_t* data, uint32_t length,
                          uint8_t* buf, size_t capacity, size_t* outLen)
{
    size_t total;

    if (!buf)
        return false;
    if (length > 0 && !data)
        return false;
    total = XLINK_FILE_DATA_HEADER + (size_t)length;
    if (capacity < total)
        return false;
    xf_putU64(&buf[0], offset);
    if (length > 0)
        memcpy(&buf[XLINK_FILE_DATA_HEADER], data, length);
    if (outLen)
        *outLen = total;
    return true;
}

bool XLinkFile_decodeData(const uint8_t* buf, size_t length,
                          uint64_t* offsetOut, const uint8_t** dataOut,
                          uint32_t* dataLenOut)
{
    if (!buf || length < XLINK_FILE_DATA_HEADER)
        return false;
    if (offsetOut)
        *offsetOut = xf_getU64(&buf[0]);
    if (dataOut)
        *dataOut = &buf[XLINK_FILE_DATA_HEADER];
    if (dataLenOut)
        *dataLenOut = (uint32_t)(length - XLINK_FILE_DATA_HEADER);
    return true;
}

bool XLinkFile_encodeAck(const XLinkFileAckPayload* msg, uint8_t* buf,
                         size_t capacity, size_t* outLen)
{
    if (!msg || !buf || capacity < 12u)
        return false;
    xf_putU64(&buf[0], msg->m_nextOffset);
    xf_putU32(&buf[8], msg->m_creditBytes);
    if (outLen)
        *outLen = 12u;
    return true;
}

bool XLinkFile_decodeAck(const uint8_t* buf, size_t length,
                         XLinkFileAckPayload* out)
{
    if (!buf || !out || length < 12u)
        return false;
    out->m_nextOffset = xf_getU64(&buf[0]);
    out->m_creditBytes = xf_getU32(&buf[8]);
    return true;
}

/* ========================================================================== */
/*                        Finish / Abort                                     */
/* ========================================================================== */

bool XLinkFile_encodeFinish(const XLinkFileFinishPayload* msg, uint8_t* buf,
                            size_t capacity, size_t* outLen)
{
    if (!msg || !buf || capacity < 40u)
        return false;
    xf_putU64(&buf[0], msg->m_totalSize);
    memcpy(&buf[8], msg->m_sha256, XLINK_AUTH_NONCE_BYTES);
    if (outLen)
        *outLen = 40u;
    return true;
}

bool XLinkFile_decodeFinish(const uint8_t* buf, size_t length,
                            XLinkFileFinishPayload* out)
{
    if (!buf || !out || length < 40u)
        return false;
    out->m_totalSize = xf_getU64(&buf[0]);
    memcpy(out->m_sha256, &buf[8], XLINK_AUTH_NONCE_BYTES);
    return true;
}

bool XLinkFile_encodeFinishAck(const XLinkFileFinishAckPayload* msg,
                               uint8_t* buf, size_t capacity, size_t* outLen)
{
    size_t textLen;
    size_t total;

    if (!msg || !buf)
        return false;
    textLen = msg->m_textLen;
    if (textLen > XLINK_FILE_TEXT_BYTES)
        textLen = XLINK_FILE_TEXT_BYTES;
    total = 4u + textLen;
    if (capacity < total)
        return false;
    buf[0] = msg->m_ok;
    buf[1] = msg->m_status;
    xf_putU16(&buf[2], (uint16_t)textLen);
    if (textLen > 0)
        memcpy(&buf[4], msg->m_text, textLen);
    if (outLen)
        *outLen = total;
    return true;
}

bool XLinkFile_decodeFinishAck(const uint8_t* buf, size_t length,
                               XLinkFileFinishAckPayload* out)
{
    uint16_t textLen;

    if (!buf || !out || length < 4u)
        return false;
    textLen = xf_getU16(&buf[2]);
    if (textLen > XLINK_FILE_TEXT_BYTES)
        return false;
    if (length < 4u + (size_t)textLen)
        return false;
    memset(out, 0, sizeof(*out));
    out->m_ok = buf[0];
    out->m_status = buf[1];
    out->m_textLen = textLen;
    if (textLen > 0)
        memcpy(out->m_text, &buf[4], textLen);
    out->m_text[textLen < XLINK_FILE_TEXT_BYTES ? textLen
                                                : XLINK_FILE_TEXT_BYTES - 1u]
        = '\0';
    return true;
}

bool XLinkFile_encodeAbort(const XLinkFileAbortPayload* msg, uint8_t* buf,
                           size_t capacity, size_t* outLen)
{
    size_t textLen;
    size_t total;

    if (!msg || !buf)
        return false;
    textLen = msg->m_textLen;
    if (textLen > XLINK_FILE_TEXT_BYTES)
        textLen = XLINK_FILE_TEXT_BYTES;
    total = 4u + textLen;
    if (capacity < total)
        return false;
    buf[0] = msg->m_status;
    buf[1] = msg->m_byPeer;
    xf_putU16(&buf[2], (uint16_t)textLen);
    if (textLen > 0)
        memcpy(&buf[4], msg->m_text, textLen);
    if (outLen)
        *outLen = total;
    return true;
}

bool XLinkFile_decodeAbort(const uint8_t* buf, size_t length,
                           XLinkFileAbortPayload* out)
{
    uint16_t textLen;

    if (!buf || !out || length < 4u)
        return false;
    textLen = xf_getU16(&buf[2]);
    if (textLen > XLINK_FILE_TEXT_BYTES)
        return false;
    if (length < 4u + (size_t)textLen)
        return false;
    memset(out, 0, sizeof(*out));
    out->m_status = buf[0];
    out->m_byPeer = buf[1];
    out->m_textLen = textLen;
    if (textLen > 0)
        memcpy(out->m_text, &buf[4], textLen);
    out->m_text[textLen < XLINK_FILE_TEXT_BYTES ? textLen
                                                : XLINK_FILE_TEXT_BYTES - 1u]
        = '\0';
    return true;
}

/* ========================================================================== */
/*                        文件名安全校验                                     */
/* ========================================================================== */

const char* XLinkFile_baseName_const(const char* name)
{
    const char* base;
    if (!name)
        return NULL;
    base = name;
    while (*name) {
        if (*name == '/' || *name == '\\')
            base = name + 1;
        ++name;
    }
    return base;
}

bool XLinkFile_isSafeName(const char* name, size_t nameLen)
{
    size_t i;
    size_t effective;

    if (!name)
        return false;
    effective = nameLen ? nameLen : XStrlen(name);
    if (effective == 0 || effective >= XLINK_FILE_NAME_BYTES)
        return false;

    /* 绝对路径 / 盘符：拒绝。 */
    if (name[0] == '/' || name[0] == '\\')
        return false;
    if (effective >= 2 && name[1] == ':'
        && ((name[0] >= 'a' && name[0] <= 'z')
            || (name[0] >= 'A' && name[0] <= 'Z')))
        return false;

    /* 控制字符与 NUL：拒绝（\0 会截断路径，是经典绕过手法）。 */
    for (i = 0; i < effective; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c == '\0' || c < 0x20 || c == 0x7F)
            return false;
    }

    /* 隐藏名（'.' 开头）与 ".." 分量：拒绝。 */
    if (name[0] == '.')
        return false;
    for (i = 0; i + 2u <= effective; ++i) {
        if (name[i] == '.' && name[i + 1u] == '.') {
            bool leftOk = (i == 0)
                          || name[i - 1u] == '/' || name[i - 1u] == '\\';
            bool rightOk = (i + 2u == effective)
                           || name[i + 2u] == '/' || name[i + 2u] == '\\';
            if (leftOk && rightOk)
                return false;
        }
    }
    return true;
}

#endif /* XLINK_ON && XLINK_FILE_ON */
