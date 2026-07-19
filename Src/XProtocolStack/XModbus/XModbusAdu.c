#include "XModbusAdu.h"
#include "XMemory.h"
#include "XCrc.h"
#include <string.h>

// =============== 辅助函数 ===============

/**
 * @brief CRC16反射（用于字节交换）
 */
static uint16_t crc_reflect(uint16_t data, int len)
{
    uint16_t ret = data & 0x01;
    for (int i = 1; i < len; i++) {
        data >>= 1;
        ret = (ret << 1) | (data & 0x01);
    }
    return ret;
}

// =============== ADU帧创建 ===============

XByteArray* XModbusAdu_createRtuFrame(int serverAddress, const XModbusPdu* pdu)
{
    if (!pdu) return NULL;

    // 获取PDU数据
    XByteArray* pduData = XModbusPdu_data(pdu);
    XModbusPdu_FunctionCode fc = XModbusPdu_functionCodeRaw(pdu);

    // 计算帧大小: 地址(1) + 功能码(1) + PDU数据 + CRC(2)
    size_t pduDataSize = pduData ? XByteArray_size_base(pduData) : 0;
    size_t frameSize = 1 + 1 + pduDataSize + 2;
    uint8_t* frame = (uint8_t*)XMalloc_System(frameSize);
    if (!frame) {
        if (pduData) XByteArray_delete_base(pduData);
        return NULL;
    }

    // 组装帧: 地址 + 功能码 + PDU数据
    frame[0] = (uint8_t)(serverAddress & 0xFF);
    frame[1] = (uint8_t)fc;
    if (pduData && pduDataSize > 0) {
        XMemory_read_data(XByteArray_data_base(pduData), XBYTE_ORDER_NATIVE, frame + 2, pduDataSize);
    }

    // 计算CRC并附加（小端序）
    uint16_t crc = XCrc_get16(frame, (uint16_t)(frameSize - 2));
    XCrc_set16Data(frame + frameSize - 2, crc, XCRC_BYTE_ORDER_LITTLE_ENDIAN);

    // 创建XByteArray返回
    XByteArray* result = XByteArray_create();
    if (result) {
        XByteArray_push_back_2(result, frame, frameSize);
    }
    XFree_System(frame);
    if (pduData) XByteArray_delete_base(pduData);

    return result;
}

XByteArray* XModbusAdu_createAsciiFrame(int serverAddress, const XModbusPdu* pdu, char delimiter)
{
    if (!pdu) return NULL;

    if (delimiter == 0) delimiter = '\n';

    // 获取PDU数据
    XByteArray* pduData = XModbusPdu_data(pdu);
    XModbusPdu_FunctionCode fc = XModbusPdu_functionCodeRaw(pdu);

    size_t pduDataSize = pduData ? XByteArray_size_base(pduData) : 0;

    // 计算二进制数据（地址 + 功能码 + PDU数据）
    size_t binSize = 1 + 1 + pduDataSize;
    uint8_t* binData = (uint8_t*)XMalloc_System(binSize);
    if (!binData) {
        if (pduData) XByteArray_delete_base(pduData);
        return NULL;
    }

    binData[0] = (uint8_t)(serverAddress & 0xFF);
    binData[1] = (uint8_t)fc;
    if (pduData && pduDataSize > 0) {
        XMemory_read_data(XByteArray_data_base(pduData), XBYTE_ORDER_NATIVE, binData + 2, pduDataSize);
    }

    // 计算LRC
    uint8_t lrc = XModbusAdu_calculateLRC(binData, (int)binSize);

    // 构建ASCII帧: ":" + hex(地址 + 功能码 + PDU数据 + LRC) + "\r" + delimiter
    // 每个字节转为2个十六进制字符，所以hex长度为 (binSize + 1) * 2
    size_t hexLen = (binSize + 1) * 2;
    // 帧: ":" (1) + hex (hexLen) + "\r" (1) + delimiter (1)
    size_t frameSize = 1 + hexLen + 1 + 1;
    uint8_t* frame = (uint8_t*)XMalloc_System(frameSize + 1); // +1 for null terminator
    if (!frame) {
        XFree_System(binData);
        if (pduData) XByteArray_delete_base(pduData);
        return NULL;
    }

    frame[0] = ':';
    // 将二进制数据转为十六进制
    const char hexChars[] = "0123456789ABCDEF";
    size_t pos = 1;
    for (size_t i = 0; i < binSize; i++) {
        frame[pos++] = hexChars[(binData[i] >> 4) & 0x0F];
        frame[pos++] = hexChars[binData[i] & 0x0F];
    }
    // 附加LRC
    frame[pos++] = hexChars[(lrc >> 4) & 0x0F];
    frame[pos++] = hexChars[lrc & 0x0F];
    frame[pos++] = '\r';
    frame[pos++] = delimiter;
    frame[pos] = '\0';

    XByteArray* result = XByteArray_create();
    if (result) {
        XByteArray_push_back_2(result, frame, frameSize);
    }

    XFree_System(frame);
    XFree_System(binData);
    if (pduData) XByteArray_delete_base(pduData);

    return result;
}

// =============== ADU解析 ===============

XModbusAdu* XModbusAdu_create(void)
{
    XModbusAdu* adu = (XModbusAdu*)XMalloc_System(sizeof(XModbusAdu));
    if (!adu) return NULL;
    memset(adu, 0, sizeof(XModbusAdu));
    adu->m_serverAddress = -1;
    return adu;
}

void XModbusAdu_free(XModbusAdu* adu)
{
    if (!adu) return;
    if (adu->m_rawData) XByteArray_delete_base(adu->m_rawData);
    if (adu->m_data) XByteArray_delete_base(adu->m_data);
    if (adu->m_pdu) XModbusPdu_delete_base(adu->m_pdu);
    XFree_System(adu);
}

XModbusAdu* XModbusAdu_parseRtu(const uint8_t* data, size_t size)
{
    if (!data || size < 4) return NULL; // 最小: 地址(1) + 功能码(1) + CRC(2)

    XModbusAdu* adu = XModbusAdu_create();
    if (!adu) return NULL;

    adu->m_type = XModbusAdu_Rtu;
    adu->m_rawData = XByteArray_create();
    if (adu->m_rawData) {
        XByteArray_push_back_2(adu->m_rawData, data, size);
    }

    // 复制数据部分（不含CRC）
    size_t dataSize = size - 2;
    adu->m_data = XByteArray_create();
    if (adu->m_data) {
        XByteArray_push_back_2(adu->m_data, data, dataSize);
    }

    // 提取地址
    adu->m_serverAddress = data[0];

    // 提取PDU（功能码 + 数据）
    size_t pduDataSize = dataSize - 1 - 1; // 减去地址(1)和功能码(1)
    XModbusPdu_FunctionCode fc = (XModbusPdu_FunctionCode)data[1];

    adu->m_pdu = XModbusResponse_create_with_code(fc);
    if (adu->m_pdu && pduDataSize > 0) {
        XModbusPdu_setData(adu->m_pdu, data + 2, pduDataSize);
    }

    // 校验CRC
    uint16_t calcCrc = XCrc_get16((uint8_t*)data, (uint16_t)dataSize);
    uint16_t rcvCrc;
    XMemory_read_data(data + dataSize, XBYTE_ORDER_LITTLE_ENDIAN, (uint8_t*)&rcvCrc, sizeof(uint16_t));
    adu->m_checksumValid = (calcCrc == rcvCrc);

    return adu;
}

XModbusAdu* XModbusAdu_parseAscii(const uint8_t* data, size_t size)
{
    if (!data || size < 9) return NULL; // 最小: ":" + 2字节hex(地址) + 2字节hex(功能码) + 2字节hex(LRC) + "\r\n"

    // 检查起始字符
    if (data[0] != ':') return NULL;

    // 查找结束符
    size_t endPos = 0;
    for (size_t i = 1; i < size; i++) {
        if (data[i] == '\r' || data[i] == '\n') {
            endPos = i;
            break;
        }
    }
    if (endPos == 0) endPos = size;

    // 十六进制字符串长度（从位置1到endPos）
    size_t hexLen = endPos - 1;
    if (hexLen < 4 || (hexLen % 2) != 0) return NULL; // 至少2字节，且为偶数

    // 将十六进制字符串转为二进制
    size_t binLen = hexLen / 2;
    uint8_t* binData = (uint8_t*)XMalloc_System(binLen);
    if (!binData) return NULL;

    for (size_t i = 0; i < binLen; i++) {
        char high = (char)data[1 + i * 2];
        char low = (char)data[1 + i * 2 + 1];
        uint8_t h = (high >= 'a') ? (high - 'a' + 10) : (high >= 'A') ? (high - 'A' + 10) : (high - '0');
        uint8_t l = (low >= 'a') ? (low - 'a' + 10) : (low >= 'A') ? (low - 'A' + 10) : (low - '0');
        binData[i] = (h << 4) | l;
    }

    // 创建ADU对象
    XModbusAdu* adu = XModbusAdu_create();
    if (!adu) {
        XFree_System(binData);
        return NULL;
    }

    adu->m_type = XModbusAdu_Ascii;

    // 原始数据（包含:和\r\n）
    adu->m_rawData = XByteArray_create();
    if (adu->m_rawData) {
        XByteArray_push_back_2(adu->m_rawData, data, endPos + 1 < size ? endPos + 1 : endPos);
    }

    // 数据部分（不含LRC）
    size_t dataSize = binLen - 1; // 减去LRC
    adu->m_data = XByteArray_create();
    if (adu->m_data) {
        XByteArray_push_back_2(adu->m_data, binData, dataSize);
    }

    // 提取地址
    adu->m_serverAddress = binData[0];

    // 提取PDU
    XModbusPdu_FunctionCode fc = (XModbusPdu_FunctionCode)binData[1];
    size_t pduDataSize = dataSize - 1 - 1; // 减去地址和功能码
    adu->m_pdu = XModbusResponse_create_with_code(fc);
    if (adu->m_pdu && pduDataSize > 0) {
        XModbusPdu_setData(adu->m_pdu, binData + 2, pduDataSize);
    }

    // 校验LRC
    uint8_t calcLrc = XModbusAdu_calculateLRC(binData, (int)dataSize);
    uint8_t rcvLrc = binData[binLen - 1];
    adu->m_checksumValid = (calcLrc == rcvLrc);

    XFree_System(binData);
    return adu;
}

// =============== 查询接口 ===============

int XModbusAdu_size(const XModbusAdu* adu)
{
    if (!adu || !adu->m_data) return -1;
    return (int)XByteArray_size_base(adu->m_data);
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

XModbusPdu* XModbusAdu_pdu(const XModbusAdu* adu)
{
    if (!adu || !adu->m_pdu) return NULL;
    return XModbusPdu_create_copy(adu->m_pdu);
}

bool XModbusAdu_matchingChecksum(const XModbusAdu* adu)
{
    return adu && adu->m_checksumValid;
}

// =============== 校验和计算 ===============

uint8_t XModbusAdu_calculateLRC(const uint8_t* data, int len)
{
    uint32_t lrc = 0;
    for (int i = 0; i < len; i++) {
        lrc += data[i];
    }
    return (uint8_t)(-(int32_t)(lrc & 0xFF));
}

uint16_t XModbusAdu_calculateCRC(const uint8_t* data, int len)
{
    return XCrc_get16((uint8_t*)data, (uint16_t)len);
}
