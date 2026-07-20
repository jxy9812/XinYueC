#include "XModbusAdu.h"
#include "XMemory.h"
#include "XCrc.h"
#include <string.h>

// =============== 内部辅助 ===============

/** 校验码字节数：ASCII=1(LRC), RTU=2(CRC) */
static inline int checksumBytes(const XModbusAdu* adu)
{
    return (adu->m_type == XModbusAdu_Ascii) ? 1 : 2;
}

/**
 * @brief 构建二进制载荷（地址 + 功能码 + PDU数据）
 * @param serverAddress 从站地址
 * @param pdu Modbus PDU
 * @param[out] outSize 输出载荷大小
 * @return 堆分配的二进制载荷，调用者负责 XFree_System 释放
 */
static uint8_t* buildBinaryPayload(int serverAddress, const XModbusPdu* pdu, size_t* outSize)
{
    XByteArray* pduData = XModbusPdu_data(pdu);
    XModbusPdu_FunctionCode fc = XModbusPdu_functionCodeRaw(pdu);

    size_t pduDataSize = pduData ? XByteArray_size_base(pduData) : 0;
    size_t binSize = 1 + 1 + pduDataSize; // 地址 + 功能码 + PDU数据

    uint8_t* binData = (uint8_t*)XMalloc_System(binSize);
    if (!binData) {
        if (pduData) XByteArray_delete_base(pduData);
        return NULL;
    }

    binData[0] = (uint8_t)(serverAddress & 0xFF);
    binData[1] = (uint8_t)fc;
    if (pduData && pduDataSize > 0) {
        XMemory_read_data(XByteArray_data(pduData), XBYTE_ORDER_NATIVE, binData + 2, pduDataSize);
    }

    if (pduData) XByteArray_delete_base(pduData);
    *outSize = binSize;
    return binData;
}

// =============== 生命周期管理 ===============

void XModbusAdu_init(XModbusAdu* adu)
{
    if (!adu) return;
    memset(adu, 0, sizeof(XModbusAdu));
    adu->m_serverAddress = 0xFF; // 0xFF表示无效
}

void XModbusAdu_delete(XModbusAdu* adu)
{
    if (!adu) return;
    XModbusAdu_deinit(adu);
    XFree_System(adu);
}

void XModbusAdu_deinit(XModbusAdu* adu)
{
    if (!adu) return;
    if (adu->m_rawData) {
        XByteArray_delete_base(adu->m_rawData);
        adu->m_rawData = NULL;
    }
    if (adu->m_data) {
        XByteArray_delete_base(adu->m_data);
        adu->m_data = NULL;
    }
}

// =============== ADU帧创建 ===============

XByteArray* XModbusAdu_createRtuFrame(int serverAddress, const XModbusPdu* pdu)
{
    if (!pdu) return NULL;

    size_t binSize = 0;
    uint8_t* binData = buildBinaryPayload(serverAddress, pdu, &binSize);
    if (!binData) return NULL;

    size_t frameSize = binSize + 2; // + CRC16
    uint8_t* frame = (uint8_t*)XMalloc_System(frameSize);
    if (!frame) {
        XFree_System(binData);
        return NULL;
    }
    memcpy(frame, binData, binSize);
    XFree_System(binData);

    uint16_t crc = XCrc_get16(frame, (uint16_t)(frameSize - 2));
    XCrc_set16Data(frame + frameSize - 2, crc, XCRC_BYTE_ORDER_LITTLE_ENDIAN);

    XByteArray* result = XByteArray_create();
    if (result)
        XByteArray_push_back_2(result, frame, frameSize);
    XFree_System(frame);
    return result;
}

XByteArray* XModbusAdu_createAsciiFrame(int serverAddress, const XModbusPdu* pdu, char delimiter)
{
    if (!pdu) return NULL;
    if (delimiter == 0) delimiter = '\n';

    size_t binSize = 0;
    uint8_t* binData = buildBinaryPayload(serverAddress, pdu, &binSize);
    if (!binData) return NULL;

    uint8_t lrc = XModbusAdu_calculateLRC(binData, (int)binSize);

    // 十六进制编码：每个字节2字符 + LRC + ":" + "\r" + delimiter
    size_t hexLen = binSize * 2;
    size_t frameSize = 1 + hexLen + 2 + 1 + 1; // ':' + hex + LRC_hex + '\r' + delimiter
    uint8_t* frame = (uint8_t*)XMalloc_System(frameSize + 1); // +1 for null terminator
    if (!frame) {
        XFree_System(binData);
        return NULL;
    }

    const char hexChars[] = "0123456789ABCDEF";
    size_t pos = 0;
    frame[pos++] = ':';
    for (size_t i = 0; i < binSize; i++) {
        frame[pos++] = (uint8_t)hexChars[(binData[i] >> 4) & 0x0F];
        frame[pos++] = (uint8_t)hexChars[binData[i] & 0x0F];
    }
    frame[pos++] = (uint8_t)hexChars[(lrc >> 4) & 0x0F];
    frame[pos++] = (uint8_t)hexChars[lrc & 0x0F];
    frame[pos++] = '\r';
    frame[pos++] = (uint8_t)delimiter;
    frame[pos] = '\0';

    XByteArray* result = XByteArray_create();
    if (result)
        XByteArray_push_back_2(result, frame, frameSize);

    XFree_System(frame);
    XFree_System(binData);
    return result;
}

// =============== ADU解析 ===============

XModbusAdu* XModbusAdu_parseRtu(const uint8_t* data, size_t size)
{
    if (!data || size < 4) return NULL;

    XModbusAdu* adu = (XModbusAdu*)XMalloc_System(sizeof(XModbusAdu));
    if (!adu) return NULL;
    XModbusAdu_init(adu);
    adu->m_type = XModbusAdu_Rtu;

    adu->m_rawData = XByteArray_create();
    if (adu->m_rawData)
        XByteArray_push_back_2(adu->m_rawData, data, size);

    adu->m_data = XByteArray_create();
    if (adu->m_data)
        XByteArray_push_back_2(adu->m_data, data, size);

    adu->m_serverAddress = data[0];

    size_t dataSize = size - 2;
    uint16_t calcCrc = XCrc_get16((uint8_t*)data, (uint16_t)dataSize);
    uint16_t rcvCrc;
    XMemory_read_data(data + dataSize, XBYTE_ORDER_LITTLE_ENDIAN, (uint8_t*)&rcvCrc, sizeof(uint16_t));
    adu->m_checksumValid = (calcCrc == rcvCrc);

    return adu;
}

XModbusAdu* XModbusAdu_parseAscii(const uint8_t* data, size_t size)
{
    if (!data || size < 9) return NULL;
    if (data[0] != ':') return NULL;

    // 查找结束位置（\r 或 \n）
    size_t endPos = 0;
    for (size_t i = 1; i < size; i++) {
        if (data[i] == '\r' || data[i] == '\n') {
            endPos = i;
            break;
        }
    }
    if (endPos == 0) endPos = size;

    size_t hexLen = endPos - 1;
    if (hexLen < 4 || (hexLen % 2) != 0) return NULL;

    size_t binLen = hexLen / 2;
    uint8_t* binData = (uint8_t*)XMalloc_System(binLen);
    if (!binData) return NULL;

    for (size_t i = 0; i < binLen; i++) {
        char high = (char)data[1 + i * 2];
        char low  = (char)data[1 + i * 2 + 1];
        uint8_t h = (high >= 'a') ? (uint8_t)(high - 'a' + 10) :
                    (high >= 'A') ? (uint8_t)(high - 'A' + 10) :
                    (uint8_t)(high - '0');
        uint8_t l = (low >= 'a') ? (uint8_t)(low - 'a' + 10) :
                    (low >= 'A') ? (uint8_t)(low - 'A' + 10) :
                    (uint8_t)(low - '0');
        binData[i] = (uint8_t)((h << 4) | l);
    }

    XModbusAdu* adu = (XModbusAdu*)XMalloc_System(sizeof(XModbusAdu));
    if (!adu) {
        XFree_System(binData);
        return NULL;
    }
    XModbusAdu_init(adu);
    adu->m_type = XModbusAdu_Ascii;

    adu->m_rawData = XByteArray_create();
    if (adu->m_rawData) {
        size_t rawLen = (endPos + 1 < size) ? (endPos + 1) : endPos;
        XByteArray_push_back_2(adu->m_rawData, data, rawLen);
    }

    adu->m_data = XByteArray_create();
    if (adu->m_data)
        XByteArray_push_back_2(adu->m_data, binData, binLen);

    adu->m_serverAddress = binData[0];

    size_t dataSize = binLen - 1;
    uint8_t calcLrc = XModbusAdu_calculateLRC(binData, (int)dataSize);
    uint8_t rcvLrc = binData[binLen - 1];
    adu->m_checksumValid = (calcLrc == rcvLrc);

    XFree_System(binData);
    return adu;
}

// =============== 查询接口（对齐Qt） ===============

int XModbusAdu_size(const XModbusAdu* adu)
{
    if (!adu || !adu->m_data) return -1;
    int sz = (int)XByteArray_size_base(adu->m_data);
    return sz - checksumBytes(adu);
}

XByteArray* XModbusAdu_data(const XModbusAdu* adu)
{
    if (!adu || !adu->m_data) return NULL;
    int sz = XModbusAdu_size(adu);
    if (sz <= 0) return NULL;
    return XByteArray_left(adu->m_data, sz);
}

int XModbusAdu_rawSize(const XModbusAdu* adu)
{
    if (!adu || !adu->m_rawData) return -1;
    return (int)XByteArray_size_base(adu->m_rawData);
}

XByteArray* XModbusAdu_rawData(const XModbusAdu* adu)
{
    if (!adu || !adu->m_rawData) return NULL;
    return XByteArray_create_copy(adu->m_rawData);
}

int XModbusAdu_serverAddress(const XModbusAdu* adu)
{
    if (!adu) return -1;
    return adu->m_serverAddress;
}

bool XModbusAdu_pdu(const XModbusAdu* adu, XModbusPdu* out)
{
    if (!adu || !adu->m_data || !out) return false;

    // 对齐Qt: QModbusPdu(FunctionCode(m_data.at(1)), m_data.mid(2, size() - 2))
    XByteArray* d = XModbusAdu_data(adu);
    if (!d) return false;

    size_t dSize = XByteArray_size_base(d);
    if (dSize < 2) {
        XByteArray_delete_base(d);
        return false;
    }

    uint8_t* raw = XByteArray_data(d);
    XModbusPdu_FunctionCode fc = (XModbusPdu_FunctionCode)raw[1];

    // 初始化输出PDU（对齐Qt：返回QModbusPdu基类）
    XModbusPdu_init_with_code(out, fc);
    if (dSize > 2)
        XModbusPdu_setData(out, raw + 2, dSize - 2);

    XByteArray_delete_base(d);
    return true;
}

bool XModbusAdu_matchingChecksum(const XModbusAdu* adu)
{
    return adu && adu->m_checksumValid;
}

// =============== 校验和计算 ===============

uint8_t XModbusAdu_calculateLRC(const uint8_t* data, int len)
{
    uint32_t lrc = 0;
    for (int i = 0; i < len; i++)
        lrc += data[i];
    return (uint8_t)(-(int32_t)(lrc & 0xFF));
}

uint16_t XModbusAdu_calculateCRC(const uint8_t* data, int len)
{
    return XCrc_get16((uint8_t*)data, (uint16_t)len);
}
