#include "XCan_config.h"
#if XPROTOCOL_ON
#if XCAN_ON
#if XCAN_DBC_ON
#include "XCanFrameProcessor.h"
#include "XMemory.h"
#include "XString.h"
#include "XStringList.h"
#include "XVariant.h"
#include <string.h>
#include <math.h>

// =============== 解析结果辅助函数 ===============

void XCanFrameProcessor_ParseResult_init(XCanFrameProcessor_ParseResult* result)
{
    if (!result) return;
    memset(result, 0, sizeof(XCanFrameProcessor_ParseResult));
    result->m_uniqueId = 0;
    result->m_signalValues = XMap_create(sizeof(XString), sizeof(XVariant), XString_compare);
    XMapBaseSetKeyCopyMethod(result->m_signalValues, XString_copy_base);
    XMapBaseSetKeyMoveMethod(result->m_signalValues, XString_move_base);
    XMapBaseSetKeyDeinitMethod(result->m_signalValues, XString_deinit_base);
}

void XCanFrameProcessor_ParseResult_deinit(XCanFrameProcessor_ParseResult* result)
{
    if (!result) return;
    if (result->m_signalValues) {
        XMap_delete_base(result->m_signalValues);
        result->m_signalValues = NULL;
    }
}

// =============== 初始化与清理 ===============

void XCanFrameProcessor_init(XCanFrameProcessor* processor)
{
    if (!processor) return;
    memset(processor, 0, sizeof(XCanFrameProcessor));
    processor->m_error = XCanFrameProcessor_Error_None;
    XCanUniqueIdDescription_init(&processor->m_uidDesc);
}

void XCanFrameProcessor_deinit(XCanFrameProcessor* processor)
{
    if (!processor) return;
    if (processor->m_messageDescriptions) {
        XVector_delete_base(processor->m_messageDescriptions);
        processor->m_messageDescriptions = NULL;
    }
    if (processor->m_errorString) {
        XString_delete_base(processor->m_errorString);
        processor->m_errorString = NULL;
    }
    if (processor->m_warnings) {
        XStringList_delete_base(processor->m_warnings);
        processor->m_warnings = NULL;
    }
}

// =============== 错误/警告查询 ===============

XCanFrameProcessor_Error XCanFrameProcessor_error(const XCanFrameProcessor* processor)
{
    return processor ? processor->m_error : XCanFrameProcessor_Error_None;
}

XString* XCanFrameProcessor_errorString(const XCanFrameProcessor* processor)
{
    if (!processor || !processor->m_errorString) return XString_create();
    return XString_create_copy(processor->m_errorString);
}

XStringList* XCanFrameProcessor_warnings(const XCanFrameProcessor* processor)
{
    if (!processor || !processor->m_warnings) return XStringList_create();
    return XStringList_create_copy(processor->m_warnings);
}

// =============== 消息描述管理 ===============

XVector* XCanFrameProcessor_messageDescriptions(const XCanFrameProcessor* processor)
{
    if (!processor || !processor->m_messageDescriptions)
        return XVector_create(sizeof(XCanMessageDescription));
    return XVector_create_copy(processor->m_messageDescriptions);
}

void XCanFrameProcessor_addMessageDescriptions(XCanFrameProcessor* processor,
    const XVector* descriptions)
{
    if (!processor || !descriptions) return;
    if (!processor->m_messageDescriptions)
        processor->m_messageDescriptions = XVector_create(sizeof(XCanMessageDescription));

    size_t count = XVector_size_base(descriptions);
    for (size_t i = 0; i < count; i++) {
        XCanMessageDescription* desc = (XCanMessageDescription*)XVector_at_base(descriptions, i);
        if (desc) {
            XVector_push_back_1_base(processor->m_messageDescriptions, desc);
        }
    }
}

void XCanFrameProcessor_setMessageDescriptions(XCanFrameProcessor* processor,
    const XVector* descriptions)
{
    if (!processor) return;
    if (processor->m_messageDescriptions) {
        XVector_delete_base(processor->m_messageDescriptions);
        processor->m_messageDescriptions = NULL;
    }
    if (descriptions)
        processor->m_messageDescriptions = XVector_create_copy(descriptions);
}

void XCanFrameProcessor_clearMessageDescriptions(XCanFrameProcessor* processor)
{
    if (!processor || !processor->m_messageDescriptions) return;
    XVector_clear_base(processor->m_messageDescriptions);
}

void XCanFrameProcessor_uniqueIdDescription(const XCanFrameProcessor* processor,
    XCanUniqueIdDescription* out)
{
    if (!processor || !out) return;
    memcpy(out, &processor->m_uidDesc, sizeof(XCanUniqueIdDescription));
}

void XCanFrameProcessor_setUniqueIdDescription(XCanFrameProcessor* processor,
    const XCanUniqueIdDescription* description)
{
    if (!processor || !description) return;
    memcpy(&processor->m_uidDesc, description, sizeof(XCanUniqueIdDescription));
}

// =============== 核心功能（简化实现） ===============

// 辅助：根据消息描述查找信号
static XCanMessageDescription* findMessageDescription(XCanFrameProcessor* processor,
    XCanBus_UniqueId uniqueId)
{
    if (!processor->m_messageDescriptions) return NULL;

    size_t count = XVector_size_base(processor->m_messageDescriptions);
    for (size_t i = 0; i < count; i++) {
        XCanMessageDescription* desc = (XCanMessageDescription*)
            XVector_at_base(processor->m_messageDescriptions, i);
        if (desc && desc->m_uniqueId == uniqueId) {
            return desc;
        }
    }
    return NULL;
}

XCanBusFrame* XCanFrameProcessor_prepareFrame(XCanFrameProcessor* processor,
    XCanBus_UniqueId uniqueId, const XMap* signalValues)
{
    if (!processor) return NULL;

    // 查找消息描述
    XCanMessageDescription* msgDesc = findMessageDescription(processor, uniqueId);
    if (!msgDesc) {
        processor->m_error = XCanFrameProcessor_Error_Encoding;
        if (processor->m_errorString) {
            XString_delete_base(processor->m_errorString);
        }
        processor->m_errorString = XString_create_fmt_utf8(
            "No message description found for unique ID 0x%X", uniqueId);
        return NULL;
    }

    // 创建帧
    XCanBusFrame* frame = XCanBusFrame_create(XCanBusFrame_DataFrame);
    if (!frame) {
        processor->m_error = XCanFrameProcessor_Error_Encoding;
        return NULL;
    }

    XCanBusFrame_setFrameId(frame, uniqueId);

    // 编码信号值到负载（简化实现）
    size_t payloadSize = msgDesc->m_size > 0 ? msgDesc->m_size : 8;
    uint8_t payload[64] = {0};

    if (msgDesc->m_signalDescriptions && signalValues) {
        size_t sigCount = XVector_size_base(msgDesc->m_signalDescriptions);
        for (size_t i = 0; i < sigCount; i++) {
            XCanSignalDescription* sigDesc = (XCanSignalDescription*)
                XVector_at_base(msgDesc->m_signalDescriptions, i);
            if (!sigDesc || !sigDesc->m_name) continue;

            // 查找信号值
            XVariant* value = (XVariant*)XMap_value_base((XMap*)signalValues, sigDesc->m_name);
            if (!value) continue;

            double physValue = XVariant_toDouble(value);
            // 反算原始值
            int64_t rawValue = (int64_t)((physValue - sigDesc->m_offset) / sigDesc->m_factor);

            // 写入负载（简化实现：小端模式）
            uint16_t startBit = sigDesc->m_startBit;
            uint16_t bitLen = sigDesc->m_bitLength;
            size_t bytePos = startBit / 8;
            size_t bitOffset = startBit % 8;
            size_t numBytes = (bitLen + 7) / 8;

            if (bytePos + numBytes > payloadSize)
                numBytes = payloadSize - bytePos;

            for (size_t b = 0; b < numBytes && b < 8; b++) {
                payload[bytePos + b] = (uint8_t)((rawValue >> (b * 8)) & 0xFF);
            }
        }
    }

    XCanBusFrame_setPayload(frame, payload, payloadSize);
    processor->m_error = XCanFrameProcessor_Error_None;
    return frame;
}

bool XCanFrameProcessor_parseFrame(XCanFrameProcessor* processor,
    const XCanBusFrame* frame, XCanFrameProcessor_ParseResult* result)
{
    if (!processor || !frame || !result) return false;

    // 检查帧有效性
    if (!XCanBusFrame_isValid(frame)) {
        processor->m_error = XCanFrameProcessor_Error_InvalidFrame;
        if (processor->m_errorString) {
            XString_delete_base(processor->m_errorString);
        }
        processor->m_errorString = XString_create_utf8("Invalid CAN frame");
        return false;
    }

    // 检查帧类型
    XCanBusFrame_FrameType type = XCanBusFrame_frameType(frame);
    if (type != XCanBusFrame_DataFrame) {
        processor->m_error = XCanFrameProcessor_Error_UnsupportedFrameFormat;
        if (processor->m_errorString) {
            XString_delete_base(processor->m_errorString);
        }
        processor->m_errorString = XString_create_utf8("Only data frames can be parsed");
        return false;
    }

    // 提取唯一 ID
    XCanBus_UniqueId uniqueId = 0;
    if (processor->m_uidDesc.m_source == XCanBus_FrameId) {
        uniqueId = XCanBusFrame_frameId(frame);
    } else {
        // 从 Payload 中提取（简化实现）
        const XByteArray* payload = XCanBusFrame_payload_const(frame);
        if (payload) {
            size_t bytePos = processor->m_uidDesc.m_startBit / 8;
            size_t bitOffset = processor->m_uidDesc.m_startBit % 8;
            size_t payloadSize = XByteArray_size_base(payload);
            if (bytePos < payloadSize) {
                const uint8_t* data = XByteArray_data((XByteArray*)payload);
                size_t numBytes = (processor->m_uidDesc.m_bitLength + 7) / 8;
                if (bytePos + numBytes > payloadSize)
                    numBytes = payloadSize - bytePos;
                for (size_t b = 0; b < numBytes; b++) {
                    uniqueId |= ((XCanBus_UniqueId)data[bytePos + b]) << (b * 8);
                }
            }
        }
    }

    result->m_uniqueId = uniqueId;

    // 查找消息描述
    XCanMessageDescription* msgDesc = findMessageDescription(processor, uniqueId);
    if (!msgDesc) {
        processor->m_error = XCanFrameProcessor_Error_Decoding;
        if (processor->m_errorString) {
            XString_delete_base(processor->m_errorString);
        }
        processor->m_errorString = XString_create_fmt_utf8(
            "No message description found for unique ID 0x%X", uniqueId);
        return false;
    }

    // 解码信号值
    const XByteArray* payload = XCanBusFrame_payload_const(frame);
    const uint8_t* data = NULL;
    size_t payloadSize = 0;
    if (payload) {
        data = XByteArray_data((XByteArray*)payload);
        payloadSize = XByteArray_size_base(payload);
    }

    if (msgDesc->m_signalDescriptions) {
        size_t sigCount = XVector_size_base(msgDesc->m_signalDescriptions);
        for (size_t i = 0; i < sigCount; i++) {
            XCanSignalDescription* sigDesc = (XCanSignalDescription*)
                XVector_at_base(msgDesc->m_signalDescriptions, i);
            if (!sigDesc || !sigDesc->m_name) continue;

            uint16_t startBit = sigDesc->m_startBit;
            uint16_t bitLen = sigDesc->m_bitLength;
            if (bitLen == 0) continue;

            // 从负载中提取值（简化实现）
            int64_t rawValue = 0;
            if (data && payloadSize > 0) {
                size_t bytePos = startBit / 8;
                size_t bitOffset = startBit % 8;
                if (bytePos < payloadSize) {
                    size_t numBytes = (bitLen + 7) / 8;
                    if (bytePos + numBytes > payloadSize)
                        numBytes = payloadSize - bytePos;
                    for (size_t b = 0; b < numBytes; b++) {
                        rawValue |= ((int64_t)data[bytePos + b]) << (b * 8);
                    }
                }
            }

            // 应用因子和偏移量
            double physicalValue = (double)rawValue * sigDesc->m_factor + sigDesc->m_offset;

            // 存储到结果
            XString sigName;
            XString_init(&sigName);
            XString_assign_utf8(&sigName, XString_toUtf8(sigDesc->m_name));

            XVariant var;
            XVariant_init(&var, NULL, 0, XVariantType_Double);
            XVariant_setValue_double(&var, physicalValue);
            XMapBase_insert_base((XMapBase*)result->m_signalValues, &sigName, &var);
            XClass_deinit_base((XClass*)&var);
            XClass_deinit_base((XClass*)&sigName);
        }
    }

    processor->m_error = XCanFrameProcessor_Error_None;
    return true;
}

#endif /* XCAN_DBC_ON */
#endif /* XCAN_ON */
#endif /* XPROTOCOL_ON */
