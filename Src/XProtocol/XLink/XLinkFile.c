/** @file       XLinkFile.c
 * @brief      XLink 文件传输扩展：载荷编解码与文件名安全校验实现。
 * @details    只依赖 XinYueC 抽象层（XStringUtils / XMemory），禁止平台 API。
 *             所有解析函数逐字段核对剩余长度后才推进，不信任线上长度字段。
 *             多字节字段一律经 XMemory_write_data/read_data 按小端线序
 *             逐字段读写（本机小端即 memcpy），字段偏移与字节宽与旧
 *             手写移位实现逐位一致，属线上格式不得改动。
 * @author     XinYueC 团队
 */
#include "XLinkFile.h"

#if XLINK_ON && XLINK_FILE_ON

#include "XStringUtils.h"
#include "XMemory.h"
#include <string.h>

/** @brief DATA 载荷上限（偏移 + 业务数据）。 */
#define XLINK_FILE_DATA_HEADER XLINK_FILE_DATA_OFFSET_BYTES

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
    XMemory_write_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&msg->m_nameLen, sizeof(msg->m_nameLen));
    XMemory_write_data(&buf[4], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&msg->m_chunkBytes, sizeof(msg->m_chunkBytes));
    XMemory_write_data(&buf[8], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&msg->m_windowBytes, sizeof(msg->m_windowBytes));
    XMemory_write_data(&buf[12], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&msg->m_totalSize, sizeof(msg->m_totalSize));
    XMemory_write_data(&buf[20], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&msg->m_startOffset, sizeof(msg->m_startOffset));
    memcpy(&buf[XLINK_FILE_OPEN_HEADER], msg->m_name, nameLen);
    if (outLen)
        *outLen = total;
    return true;
}

bool XLinkFile_decodeOpen(const uint8_t* buf, size_t length,
                          XLinkFileOpenPayload* out)
{
    uint16_t nameLen = 0;

    if (!buf || !out || length < XLINK_FILE_OPEN_HEADER)
        return false;
    if (buf[0] != XLinkFileMode_Upload && buf[0] != XLinkFileMode_Download)
        return false;
    XMemory_read_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&nameLen, sizeof(nameLen));
    if (nameLen == 0 || nameLen >= XLINK_FILE_NAME_BYTES)
        return false;
    if (length < XLINK_FILE_OPEN_HEADER + (size_t)nameLen)
        return false;

    memset(out, 0, sizeof(*out));
    out->m_mode = buf[0];
    out->m_flags = buf[1];
    out->m_nameLen = nameLen;
    XMemory_read_data(&buf[4], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&out->m_chunkBytes, sizeof(out->m_chunkBytes));
    XMemory_read_data(&buf[8], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&out->m_windowBytes, sizeof(out->m_windowBytes));
    XMemory_read_data(&buf[12], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&out->m_totalSize, sizeof(out->m_totalSize));
    XMemory_read_data(&buf[20], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&out->m_startOffset, sizeof(out->m_startOffset));
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
    uint16_t textLenU16;
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
    textLenU16 = (uint16_t)textLen;
    XMemory_write_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&textLenU16, sizeof(textLenU16));
    XMemory_write_data(&buf[4], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&msg->m_chunkBytes, sizeof(msg->m_chunkBytes));
    XMemory_write_data(&buf[8], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&msg->m_windowBytes, sizeof(msg->m_windowBytes));
    XMemory_write_data(&buf[12], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&msg->m_startOffset, sizeof(msg->m_startOffset));
    XMemory_write_data(&buf[20], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&msg->m_totalSize, sizeof(msg->m_totalSize));
    if (textLen > 0)
        memcpy(&buf[XLINK_FILE_OPEN_ACK_HEADER], msg->m_text, textLen);
    if (outLen)
        *outLen = total;
    return true;
}

bool XLinkFile_decodeOpenAck(const uint8_t* buf, size_t length,
                             XLinkFileOpenAckPayload* out)
{
    uint16_t textLen = 0;

    if (!buf || !out || length < XLINK_FILE_OPEN_ACK_HEADER)
        return false;
    XMemory_read_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&textLen, sizeof(textLen));
    if (textLen > XLINK_FILE_TEXT_BYTES)
        return false;
    if (length < XLINK_FILE_OPEN_ACK_HEADER + (size_t)textLen)
        return false;

    memset(out, 0, sizeof(*out));
    out->m_accepted = buf[0];
    out->m_status = buf[1];
    out->m_textLen = textLen;
    XMemory_read_data(&buf[4], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&out->m_chunkBytes, sizeof(out->m_chunkBytes));
    XMemory_read_data(&buf[8], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&out->m_windowBytes, sizeof(out->m_windowBytes));
    XMemory_read_data(&buf[12], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&out->m_startOffset, sizeof(out->m_startOffset));
    XMemory_read_data(&buf[20], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&out->m_totalSize, sizeof(out->m_totalSize));
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
    XMemory_write_data(&buf[0], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&offset, sizeof(offset));
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
        XMemory_read_data(&buf[0], XBYTE_ORDER_LITTLE_ENDIAN,
                          (uint8_t*)offsetOut, sizeof(*offsetOut));
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
    XMemory_write_data(&buf[0], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&msg->m_nextOffset, sizeof(msg->m_nextOffset));
    XMemory_write_data(&buf[8], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&msg->m_creditBytes, sizeof(msg->m_creditBytes));
    if (outLen)
        *outLen = 12u;
    return true;
}

bool XLinkFile_decodeAck(const uint8_t* buf, size_t length,
                         XLinkFileAckPayload* out)
{
    if (!buf || !out || length < 12u)
        return false;
    XMemory_read_data(&buf[0], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&out->m_nextOffset, sizeof(out->m_nextOffset));
    XMemory_read_data(&buf[8], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&out->m_creditBytes, sizeof(out->m_creditBytes));
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
    XMemory_write_data(&buf[0], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&msg->m_totalSize, sizeof(msg->m_totalSize));
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
    XMemory_read_data(&buf[0], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&out->m_totalSize, sizeof(out->m_totalSize));
    memcpy(out->m_sha256, &buf[8], XLINK_AUTH_NONCE_BYTES);
    return true;
}

bool XLinkFile_encodeFinishAck(const XLinkFileFinishAckPayload* msg,
                               uint8_t* buf, size_t capacity, size_t* outLen)
{
    uint16_t textLenU16;
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
    textLenU16 = (uint16_t)textLen;
    XMemory_write_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&textLenU16, sizeof(textLenU16));
    if (textLen > 0)
        memcpy(&buf[4], msg->m_text, textLen);
    if (outLen)
        *outLen = total;
    return true;
}

bool XLinkFile_decodeFinishAck(const uint8_t* buf, size_t length,
                               XLinkFileFinishAckPayload* out)
{
    uint16_t textLen = 0;

    if (!buf || !out || length < 4u)
        return false;
    XMemory_read_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&textLen, sizeof(textLen));
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
    uint16_t textLenU16;
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
    textLenU16 = (uint16_t)textLen;
    XMemory_write_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&textLenU16, sizeof(textLenU16));
    if (textLen > 0)
        memcpy(&buf[4], msg->m_text, textLen);
    if (outLen)
        *outLen = total;
    return true;
}

bool XLinkFile_decodeAbort(const uint8_t* buf, size_t length,
                           XLinkFileAbortPayload* out)
{
    uint16_t textLen = 0;

    if (!buf || !out || length < 4u)
        return false;
    XMemory_read_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&textLen, sizeof(textLen));
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
