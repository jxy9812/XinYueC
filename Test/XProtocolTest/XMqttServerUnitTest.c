#include "XProtocolTest.h"
#include "XMqttServer.h"
#include "XMqttServer_p.h"
#include "XByteArray.h"
#include "XMemory.h"
#include "XPrintf.h"
#include "XDateTime.h"
#include "XThread.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * XMqttServer 单元测试（内存 Mock 传输）
 * ========================================================================
 * 通过重写 XMqttServer_sendData_base / XMqttServer_closeClient_base 捕获
 * 服务器输出，不依赖任何平台/网络 API。每个客户端传输使用一个固定编号
 * 的 void* 指针标识，输出缓冲按传输编号保存在 Mock 结构体中。
 */

#define XMQTT_MOCK_SLOTS 4

XCLASS_DEFINE_BEGING(XMqttServerMock)
XCLASS_DEFINE_EXTEND_END(XMqttServerMock, XMqttServer)

typedef struct XMqttServerMockSlot {
    void* transport;         ///< 传输标识指针（借用，仅作编号）。
    XByteArray* output;      ///< 服务器发送给该传输的字节流。
} XMqttServerMockSlot;

typedef struct XMqttServerMock {
    XMqttServer m_base;                        ///< 基类（MQTT 引擎）。
    XMqttServerMockSlot slots[XMQTT_MOCK_SLOTS]; ///< 传输输出槽。
    int closeCount;                            ///< closeClient 调用次数。
} XMqttServerMock;

/* ==================== Mock 传输实现 ==================== */

static XMqttServerMockSlot* mock_slot_find(XMqttServerMock* mock, void* transport)
{
    int i;
    if (!mock || !transport) return NULL;
    for (i = 0; i < XMQTT_MOCK_SLOTS; ++i) {
        if (mock->slots[i].transport == transport)
            return &mock->slots[i];
    }
    return NULL;
}

static XMqttServerMockSlot* mock_slot_ensure(XMqttServerMock* mock, void* transport)
{
    XMqttServerMockSlot* slot;
    int i;
    if (!mock || !transport) return NULL;
    slot = mock_slot_find(mock, transport);
    if (slot) return slot;
    for (i = 0; i < XMQTT_MOCK_SLOTS; ++i) {
        if (!mock->slots[i].transport) {
            mock->slots[i].transport = transport;
            mock->slots[i].output = XByteArray_create();
            return mock->slots[i].output ? &mock->slots[i] : NULL;
        }
    }
    return NULL;
}

static bool V_mock_sendData(XMqttServer* base, void* transport,
                            const uint8_t* data, size_t size)
{
    XMqttServerMock* mock = (XMqttServerMock*)base;
    XMqttServerMockSlot* slot;
    if (!base || (!data && size)) return false;
    slot = mock_slot_ensure(mock, transport);
    if (!slot || !slot->output) return false;
    if (size && !XByteArray_push_back_2(slot->output, data, size)) return false;
    return true;
}

static void V_mock_closeClient(XMqttServer* base, void* transport)
{
    XMqttServerMock* mock = (XMqttServerMock*)base;
    (void)transport;
    if (mock) ++mock->closeCount;
}

static void V_mock_deinit(XMqttServer* base)
{
    XMqttServerMock* mock = (XMqttServerMock*)base;
    int i;
    if (mock) {
        for (i = 0; i < XMQTT_MOCK_SLOTS; ++i) {
            if (mock->slots[i].output) {
                XByteArray_delete_base(mock->slots[i].output);
                mock->slots[i].output = NULL;
            }
            mock->slots[i].transport = NULL;
        }
    }
    XClass_Deinit_Parent(XMqttServer, mock);
}

static XVtable* XMqttServerMock_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMqttServerMock)
    XVTABLE_INHERIT_XCLASS(XMqttServer);
    XVTABLE_OVERLOAD_DEFAULT(EXMqttServer_SendData, V_mock_sendData);
    XVTABLE_OVERLOAD_DEFAULT(EXMqttServer_CloseClient, V_mock_closeClient);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, V_mock_deinit);
    return XVTABLE_DEFAULT;
}

static XMqttServerMock* mock_server_create(void)
{
    XMqttServerMock* mock =
        (XMqttServerMock*)XMemory_malloc(sizeof(XMqttServerMock), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!mock) return NULL;
    memset(mock, 0, sizeof(*mock));
    XMqttServer_init(&mock->m_base);
    XClassGetVtable(mock) = XMqttServerMock_class_init();
    Set_Class_Memory(mock, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(mock, true);
    return mock;
}

static void mock_server_delete(XMqttServerMock* mock)
{
    if (mock) XClass_delete_base((XClass*)mock);
}

static XByteArray* mock_output(XMqttServerMock* mock, void* transport)
{
    XMqttServerMockSlot* slot = mock_slot_find(mock, transport);
    return slot ? slot->output : NULL;
}

static void mock_clear_output(XMqttServerMock* mock, void* transport)
{
    XByteArray* out = mock_output(mock, transport);
    if (out) XByteArray_remove_base(out, 0, (int64_t)XByteArray_size_base(out));
}

/* ==================== Mock 传输标识 ==================== */

#define T1 ((void*)(uintptr_t)0x1001)
#define T2 ((void*)(uintptr_t)0x1002)
#define T3 ((void*)(uintptr_t)0x1003)

/* ==================== MQTT 报文构造辅助 ==================== */

static bool mock_append_varint(XByteArray* out, uint32_t value)
{
    uint8_t byte;
    if (!out) return false;
    do {
        byte = (uint8_t)(value % 128);
        value /= 128;
        if (value) byte |= 0x80U;
        if (!XByteArray_push_back_1(out, byte)) return false;
    } while (value);
    return true;
}

static bool mock_append_u8(XByteArray* out, uint8_t value)
{
    return out && XByteArray_push_back_1(out, value);
}

static bool mock_append_u16(XByteArray* out, uint16_t value)
{
    return mock_append_u8(out, (uint8_t)(value >> 8)) &&
           mock_append_u8(out, (uint8_t)value);
}

static bool mock_append_bytes(XByteArray* out, const void* data, size_t size)
{
    if (!out) return false;
    if (!size) return true;
    return XByteArray_push_back_2(out, data, size);
}

static bool mock_append_string(XByteArray* out, const char* utf8)
{
    size_t len = utf8 ? strlen(utf8) : 0;
    if (len > 65535) return false;
    return mock_append_u16(out, (uint16_t)len) &&
           (len == 0 || mock_append_bytes(out, utf8, len));
}

static bool mock_append_binary(XByteArray* out, const uint8_t* data, size_t size)
{
    if (size > 65535) return false;
    return mock_append_u16(out, (uint16_t)size) &&
           (size == 0 || mock_append_bytes(out, data, size));
}

static XByteArray* mock_packet(uint8_t header, const XByteArray* payload)
{
    XByteArray* packet = XByteArray_create();
    size_t size;
    if (!packet) return NULL;
    size = payload ? XByteArray_size_base(payload) : 0;
    if (!mock_append_u8(packet, header) ||
        !mock_append_varint(packet, (uint32_t)size) ||
        (size && !mock_append_bytes(packet,
            payload ? XByteArray_constData((XByteArray*)payload) : NULL, size))) {
        XByteArray_delete_base(packet);
        return NULL;
    }
    return packet;
}

static XByteArray* mock_connect_v4(const char* clientId, bool clean, uint16_t keepAlive)
{
    XByteArray* body = XByteArray_create();
    XByteArray* packet;
    uint8_t flags = clean ? 0x02U : 0x00U;
    if (!body) return NULL;
    if (!mock_append_string(body, "MQTT") ||
        !mock_append_u8(body, 4) ||
        !mock_append_u8(body, flags) ||
        !mock_append_u16(body, keepAlive) ||
        !mock_append_string(body, clientId)) {
        XByteArray_delete_base(body);
        return NULL;
    }
    packet = mock_packet(0x10, body);
    XByteArray_delete_base(body);
    return packet;
}

static XByteArray* mock_connect_v5(const char* clientId, bool cleanStart,
                                   uint16_t keepAlive, uint32_t sessionExpiry)
{
    XByteArray* body = XByteArray_create();
    XByteArray* props = XByteArray_create();
    XByteArray* packet;
    uint8_t flags = cleanStart ? 0x02U : 0x00U;
    if (!body || !props) goto fail;
    /* 连接属性：会话过期间隔（可选） */
    if (!mock_append_string(body, "MQTT") ||
        !mock_append_u8(body, 5) ||
        !mock_append_u8(body, flags) ||
        !mock_append_u16(body, keepAlive)) goto fail;
    if (sessionExpiry) {
        if (!mock_append_u8(props, 0x11) ||
            !mock_append_u8(props, (uint8_t)(sessionExpiry >> 24)) ||
            !mock_append_u8(props, (uint8_t)(sessionExpiry >> 16)) ||
            !mock_append_u8(props, (uint8_t)(sessionExpiry >> 8)) ||
            !mock_append_u8(props, (uint8_t)sessionExpiry)) goto fail;
    }
    if (!mock_append_u8(body, (uint8_t)XByteArray_size_base(props)) ||
        (XByteArray_size_base(props) &&
         !mock_append_bytes(body, XByteArray_constData(props),
                            (size_t)XByteArray_size_base(props)))) goto fail;
    if (!mock_append_string(body, clientId)) goto fail;
    packet = mock_packet(0x10, body);
    XByteArray_delete_base(body);
    XByteArray_delete_base(props);
    return packet;
fail:
    if (body) XByteArray_delete_base(body);
    if (props) XByteArray_delete_base(props);
    return NULL;
}

static XByteArray* mock_publish(const char* topic, const uint8_t* payload,
                                size_t payloadSize, uint8_t qos, bool retain,
                                uint16_t packetId)
{
    XByteArray* body = XByteArray_create();
    XByteArray* packet;
    uint8_t header = 0x30U | (uint8_t)((qos & 0x03U) << 1) | (retain ? 0x01U : 0);
    if (!body) return NULL;
    if (!mock_append_string(body, topic)) goto fail;
    if (qos && !mock_append_u16(body, packetId)) goto fail;
    if (payloadSize && !mock_append_bytes(body, payload, payloadSize)) goto fail;
    packet = mock_packet(header, body);
    XByteArray_delete_base(body);
    return packet;
fail:
    XByteArray_delete_base(body);
    return NULL;
}

typedef struct MockSubscribeItem {
    const char* filter;
    uint8_t qos;
} MockSubscribeItem;

static XByteArray* mock_subscribe(uint16_t packetId, const MockSubscribeItem* items, size_t count)
{
    XByteArray* body = XByteArray_create();
    XByteArray* packet;
    size_t i;
    if (!body || !mock_append_u16(body, packetId)) goto fail;
    for (i = 0; i < count; ++i) {
        if (!mock_append_string(body, items[i].filter) ||
            !mock_append_u8(body, items[i].qos & 0x03U)) goto fail;
    }
    packet = mock_packet(0x82, body);
    XByteArray_delete_base(body);
    return packet;
fail:
    XByteArray_delete_base(body);
    return NULL;
}

static XByteArray* mock_subscribe_v5(uint16_t packetId,
                                        const MockSubscribeItem* items, size_t count)
{
    XByteArray* body = XByteArray_create();
    XByteArray* packet;
    size_t i;
    if (!body || !mock_append_u16(body, packetId)) goto fail;
    /* MQTT 5.0 SUBSCRIBE 属性块（长度 0） */
    if (!mock_append_u8(body, 0)) goto fail;
    for (i = 0; i < count; ++i) {
        if (!mock_append_string(body, items[i].filter) ||
            !mock_append_u8(body, items[i].qos & 0x03U)) goto fail;
    }
    packet = mock_packet(0x82, body);
    XByteArray_delete_base(body);
    return packet;
fail:
    XByteArray_delete_base(body);
    return NULL;
}

static XByteArray* mock_unsubscribe(uint16_t packetId, const char* const* filters, size_t count)
{
    XByteArray* body = XByteArray_create();
    XByteArray* packet;
    size_t i;
    if (!body || !mock_append_u16(body, packetId)) goto fail;
    for (i = 0; i < count; ++i) {
        if (!mock_append_string(body, filters[i])) goto fail;
    }
    packet = mock_packet(0xA2, body);
    XByteArray_delete_base(body);
    return packet;
fail:
    XByteArray_delete_base(body);
    return NULL;
}

static XByteArray* mock_ack(uint8_t type, uint16_t packetId)
{
    XByteArray* body = XByteArray_create();
    XByteArray* packet;
    if (!body || !mock_append_u16(body, packetId)) goto fail;
    packet = mock_packet(type, body);
    XByteArray_delete_base(body);
    return packet;
fail:
    XByteArray_delete_base(body);
    return NULL;
}

static XByteArray* mock_pingreq(void)
{
    return mock_packet(0xC0, NULL);
}

static XByteArray* mock_disconnect(void)
{
    return mock_packet(0xE0, NULL);
}

/* ==================== 输出解析辅助 ==================== */

/**
 * @brief 从字节流中解析出完整 MQTT 报文。
 * @param data 字节流。
 * @param size 字节流大小。
 * @param offset 输入时起始偏移，输出时移动到下一报文起始位置。
 * @param header 输出固定头字节。
 * @param body 输出报文载荷（新建，调用者释放）；无载荷时为 NULL。
 * @return 解析成功返回 true。
 */
static bool mock_parse_packet(const uint8_t* data, size_t size, size_t* offset,
                              uint8_t* header, XByteArray** body)
{
    size_t pos;
    size_t multiplier = 1;
    size_t bodySize = 0;
    size_t bodyStart;
    uint8_t byte;
    if (!data || !offset || !header || !body) return false;
    pos = *offset;
    if (pos >= size) return false;
    *header = data[pos++];
    if (pos >= size) return false;
    do {
        if (pos >= size || pos > *offset + 5) return false;
        byte = data[pos++];
        bodySize += (size_t)(byte & 0x7fU) * multiplier;
        multiplier *= 128;
    } while (byte & 0x80U);
    if (bodySize > size - pos) return false;
    bodyStart = pos;
    *offset = pos + bodySize;
    if (bodySize) {
        *body = XByteArray_create_with_data((const char*)data + bodyStart, bodySize);
        return *body != NULL;
    }
    *body = NULL;
    return true;
}

/**
 * @brief 统计输出流中指定类型的报文数量。
 */
static int mock_count_packets(XByteArray* out, uint8_t type)
{
    size_t offset = 0;
    int count = 0;
    const uint8_t* data;
    size_t size;
    if (!out) return 0;
    data = (const uint8_t*)XByteArray_constData(out);
    size = (size_t)XByteArray_size_base(out);
    while (offset < size) {
        uint8_t header;
        XByteArray* body = NULL;
        if (!mock_parse_packet(data, size, &offset, &header, &body)) break;
        if ((header & 0xF0U) == type) ++count;
        if (body) XByteArray_delete_base(body);
    }
    return count;
}

/**
 * @brief 取出输出流中第一个指定类型的报文载荷。
 * @return true 表示找到；bodyOut 为新建载荷（无载荷时为 NULL）。
 */
static bool mock_find_packet(XByteArray* out, uint8_t type, XByteArray** bodyOut)
{
    size_t offset = 0;
    const uint8_t* data;
    size_t size;
    if (!out || !bodyOut) return false;
    *bodyOut = NULL;
    data = (const uint8_t*)XByteArray_constData(out);
    size = (size_t)XByteArray_size_base(out);
    while (offset < size) {
        uint8_t header;
        XByteArray* body = NULL;
        if (!mock_parse_packet(data, size, &offset, &header, &body)) break;
        if ((header & 0xF0U) == type) {
            *bodyOut = body;
            return true;
        }
        if (body) XByteArray_delete_base(body);
    }
    return false;
}

/**
 * @brief 取出输出流中第一个指定类型报文的报文头与载荷。
 * @return true 表示找到；headerOut 输出报文头字节，bodyOut 为新建载荷（无载荷时为 NULL）。
 */
static bool mock_find_packet_header(XByteArray* out, uint8_t type,
                                    uint8_t* headerOut, XByteArray** bodyOut)
{
    size_t offset = 0;
    const uint8_t* data;
    size_t size;
    if (!out || !bodyOut || !headerOut) return false;
    *bodyOut = NULL;
    data = (const uint8_t*)XByteArray_constData(out);
    size = (size_t)XByteArray_size_base(out);
    while (offset < size) {
        uint8_t header;
        XByteArray* body = NULL;
        if (!mock_parse_packet(data, size, &offset, &header, &body)) break;
        if ((header & 0xF0U) == type) {
            *headerOut = header;
            *bodyOut = body;
            return true;
        }
        if (body) XByteArray_delete_base(body);
    }
    return false;
}

static uint16_t mock_body_u16(const XByteArray* body, size_t offset)
{
    const uint8_t* data;
    if (!body || offset + 2 > (size_t)XByteArray_size_base(body)) return 0;
    data = (const uint8_t*)XByteArray_constData((XByteArray*)body);
    return (uint16_t)((data[offset] << 8) | data[offset + 1]);
}


/**
 * @brief 以 v5 CONNECT 建立 mock 客户端连接（可选保留 CONNACK 输出）。
 * @param mock Mock 服务器实例。
 * @param transport 传输标识。
 * @param clientId 客户端 ID。
 * @param sessionExpiry v5 会话过期间隔（秒），0 表示不携带。
 * @param clearConnack 是否在返回前清空 CONNACK 输出。
 * @return 连接并收到 CONNACK 返回 true。
 */
static bool mock_connect_v5_existing_ex(XMqttServerMock* mock, void* transport,
                                        const char* clientId, uint32_t sessionExpiry,
                                        bool clearConnack)
{
    XByteArray* connect;
    XByteArray* connack;
    if (!mock) return false;
    if (!XMqttServer_beginClient((XMqttServer*)mock, transport)) return false;
    connect = mock_connect_v5(clientId, true, 0, sessionExpiry);
    if (!connect) return false;
    XMqttServer_feedData((XMqttServer*)mock, transport,
                         (const uint8_t*)XByteArray_constData(connect),
                         (size_t)XByteArray_size_base(connect));
    XByteArray_delete_base(connect);
    if (!mock_find_packet(mock_output(mock, transport), 0x20, &connack)) return false;
    XByteArray_delete_base(connack);
    if (clearConnack) mock_clear_output(mock, transport);
    return true;
}

/**
 * @brief 构造 v5 PUBLISH 报文（属性块由调用方提供，仅支持单字节长度）。
 * @param topic 主题名。
 * @param props 属性块字节（不含长度前缀），可为 NULL。
 * @param payload 载荷，payloadSize 为 0 时可为 NULL。
 * @param qos 发布 QoS（0/1/2）。
 * @param retain 保留标志。
 * @param packetId 包 ID（qos>0 时必须非 0）。
 * @return 新建报文，失败返回 NULL。
 */
static XByteArray* mock_publish_v5(const char* topic, const XByteArray* props,
                                   const uint8_t* payload, size_t payloadSize,
                                   uint8_t qos, bool retain, uint16_t packetId)
{
    XByteArray* body = XByteArray_create();
    XByteArray* packet;
    uint8_t header = 0x30U | (uint8_t)((qos & 0x03U) << 1) | (retain ? 0x01U : 0);
    size_t propsSize = props ? XByteArray_size_base(props) : 0;
    if (!body) return NULL;
    if (!mock_append_string(body, topic)) goto fail;
    if (qos && !mock_append_u16(body, packetId)) goto fail;
    if (!mock_append_u8(body, (uint8_t)propsSize)) goto fail;
    if (propsSize && !mock_append_bytes(body, XByteArray_constData((XByteArray*)props),
                                        propsSize)) goto fail;
    if (payloadSize && !mock_append_bytes(body, payload, payloadSize)) goto fail;
    packet = mock_packet(header, body);
    XByteArray_delete_base(body);
    return packet;
fail:
    XByteArray_delete_base(body);
    return NULL;
}

/**
 * @brief 判断字节缓冲区中是否包含指定字节序列。
 * @param haystack 待搜索的字节缓冲区。
 * @param needle 目标字节序列。
 * @param needleSize 目标字节序列长度。
 * @return 包含返回 true。
 */
static bool mock_body_contains(const XByteArray* haystack, const uint8_t* needle,
                               size_t needleSize)
{
    const uint8_t* data;
    size_t size;
    size_t i;
    if (!haystack || !needle) return false;
    if (!needleSize) return true;
    data = (const uint8_t*)XByteArray_constData((XByteArray*)haystack);
    size = (size_t)XByteArray_size_base(haystack);
    if (needleSize > size) return false;
    for (i = 0; i + needleSize <= size; ++i) {
        if (memcmp(data + i, needle, needleSize) == 0) return true;
    }
    return false;
}

/**
 * @brief 在 v5 CONNACK 载荷中查找指定属性。
 * @details 覆盖服务器 CONNACK 会发出的属性：分配客户端 ID(0x12)、服务器保活(0x13)、
 *          主题别名上限(0x22)、最大 QoS(0x24)、保留可用(0x25)、最大报文大小(0x27)、
 *          通配符可用(0x28)、订阅标识符可用(0x29)、共享订阅可用(0x2A)。
 * @param body CONNACK 载荷（会话标志+原因码+属性块）。
 * @param propId 属性 ID。
 * @param valueOut 输出属性值起始指针（属性 ID 之后），可为 NULL。
 * @param valueLenOut 输出属性值字节数，可为 NULL。
 * @return 找到返回 true。
 */
static bool mock_connack_find_property(const XByteArray* body, uint8_t propId,
                                       const uint8_t** valueOut, size_t* valueLenOut)
{
    const uint8_t* data;
    size_t size;
    size_t pos = 2;
    size_t plen = 0;
    size_t valueLen = 0;
    uint32_t multiplier = 1;
    if (!body || XByteArray_size_base(body) < 3) return false;
    data = (const uint8_t*)XByteArray_constData((XByteArray*)body);
    size = (size_t)XByteArray_size_base(body);
    do {
        if (pos >= size) return false;
        plen += (size_t)(data[pos] & 0x7fU) * multiplier;
        multiplier *= 128U;
    } while (data[pos++] & 0x80U);
    if (plen > size - pos) return false;
    size = pos + plen;
    while (pos < size) {
        uint8_t id = data[pos++];
        if (id == propId) {
            if (valueOut) *valueOut = data + pos;
            if (valueLenOut) *valueLenOut = size - pos;
            return true;
        }
        switch (id) {
        case 0x12: /* 分配客户端 ID：字符串 */
            if (pos + 2 > size) return false;
            valueLen = ((size_t)data[pos] << 8) | data[pos + 1];
            pos += 2;
            break;
        case 0x13: case 0x22: valueLen = 2; break;            /* u16 */
        case 0x24: case 0x25: case 0x28: case 0x29: case 0x2A: valueLen = 1; break; /* u8 */
        case 0x27: valueLen = 4; break;                        /* u32 */
        default: return false;
        }
        if (valueLen > size - pos) return false;
        pos += valueLen;
    }
    return false;
}

/**
 * @brief 读取 SUBACK 载荷中指定索引的订阅原因码。
 * @param body SUBACK 载荷。
 * @param v5 是否为 v5 SUBACK（v5 在包 ID 后有 1 字节属性块长度）。
 * @param index 订阅项索引（从 0 开始）。
 * @return 原因码，解析失败返回 0xFF。
 */
static uint8_t mock_suback_reason(const XByteArray* body, bool v5, size_t index)
{
    const uint8_t* data;
    size_t size;
    size_t offset = 2;
    if (!body) return 0xFF;
    data = (const uint8_t*)XByteArray_constData((XByteArray*)body);
    size = (size_t)XByteArray_size_base(body);
    if (v5) {
        if (offset >= size) return 0xFF;
        offset += 1 + (size_t)data[offset]; /* 属性块长度（服务器恒为 0，单字节足够） */
    }
    offset += index;
    return offset < size ? data[offset] : 0xFF;
}

/* ==================== 测试工具 ==================== */

static int g_mockPass;
static int g_mockFail;

#define MOCK_CHECK(cond, text) do { \
    if (cond) { XPrintf("  [通过] %s\n", text); ++g_mockPass; } \
    else { XPrintf("  [失败] %s\n", text); ++g_mockFail; } \
} while (0)

static bool mock_connect_existing_ex(XMqttServerMock* mock, void* transport,
                                        const char* clientId, bool clean,
                                        uint16_t keepAlive, bool clearConnack)
{
    XByteArray* connect;
    XByteArray* connack;
    if (!mock) return false;
    if (!XMqttServer_beginClient((XMqttServer*)mock, transport)) return false;
    connect = mock_connect_v4(clientId, clean, keepAlive);
    if (!connect) return false;
    XMqttServer_feedData((XMqttServer*)mock, transport,
                         (const uint8_t*)XByteArray_constData(connect),
                         (size_t)XByteArray_size_base(connect));
    XByteArray_delete_base(connect);
    if (!mock_find_packet(mock_output(mock, transport), 0x20, &connack)) return false;
    XByteArray_delete_base(connack);
    if (clearConnack) mock_clear_output(mock, transport);
    return true;
}

static bool mock_connect_existing(XMqttServerMock* mock, void* transport,
                                     const char* clientId, bool clean, uint16_t keepAlive)
{
    return mock_connect_existing_ex(mock, transport, clientId, clean, keepAlive, true);
}

/* ==================== 测试用例 ==================== */

/* 1. CONNECT/CONNACK（v4 与 v5） */
static void mock_test_connect(void)
{
    XMqttServerMock* mock;
    XByteArray* packet;
    XByteArray* body = NULL;
    XPrintf("---- CONNECT/CONNACK ----\n");

    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    XMqttServer_beginClient((XMqttServer*)mock, T1);
    packet = mock_connect_v4("client-1", true, 60);
    MOCK_CHECK(packet != NULL, "构造 v4 CONNECT");
    if (packet) {
        XMqttServer_feedData((XMqttServer*)mock, T1,
                             (const uint8_t*)XByteArray_constData(packet),
                             (size_t)XByteArray_size_base(packet));
        XByteArray_delete_base(packet);
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x20, &body),
               "收到 CONNACK");
    if (body) {
        MOCK_CHECK(XByteArray_size_base(body) == 2 && XByteArray_constData(body)[0] == 0 && XByteArray_constData(body)[1] == 0,
                   "CONNACK v4 成功（会话标志 0、原因码 0）");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_clear_output(mock, T1);

    /* v5 CONNECT：使用新传输（同一连接上重复 CONNECT 属协议违规，服务器不回复） */
    XMqttServer_beginClient((XMqttServer*)mock, T2);
    packet = mock_connect_v5("client-5", true, 60, 0);
    MOCK_CHECK(packet != NULL, "构造 v5 CONNECT");
    if (packet) {
        XMqttServer_feedData((XMqttServer*)mock, T2,
                             (const uint8_t*)XByteArray_constData(packet),
                             (size_t)XByteArray_size_base(packet));
        XByteArray_delete_base(packet);
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T2), 0x20, &body),
               "v5 收到 CONNACK");
    if (body) {
        MOCK_CHECK(XByteArray_size_base(body) >= 2 && XByteArray_constData(body)[0] == 0 && XByteArray_constData(body)[1] == 0,
                   "CONNACK v5 会话标志/原因码正确");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_server_delete(mock);
}

/* 2. PUBLISH QoS0 路由（两个客户端） */
static void mock_test_publish_qos0(void)
{
    XMqttServerMock* mock;
    XByteArray* sub;
    XByteArray* pub;
    XByteArray* body = NULL;
    uint8_t header = 0;
    static const uint8_t payload[] = "hello-qos0";
    MockSubscribeItem item = {"topic/#", 0};
    XPrintf("---- PUBLISH QoS0 路由 ----\n");

    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    MOCK_CHECK(mock_connect_existing(mock, T1, "sub-1", true, 0), "订阅者 CONNECT 成功");
    MOCK_CHECK(mock_connect_existing(mock, T2, "pub-1", true, 0), "发布者 CONNECT 成功");

    sub = mock_subscribe(1, &item, 1);
    MOCK_CHECK(sub != NULL, "构造 SUBSCRIBE topic/#");
    if (sub) {
        XMqttServer_feedData((XMqttServer*)mock, T1,
                             (const uint8_t*)XByteArray_constData(sub),
                             (size_t)XByteArray_size_base(sub));
        XByteArray_delete_base(sub);
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x90, &body),
               "订阅者收到 SUBACK");
    if (body) XByteArray_delete_base(body);
    mock_clear_output(mock, T1);

    pub = mock_publish("topic/hello", payload, sizeof(payload) - 1, 0, false, 0);
    MOCK_CHECK(pub != NULL, "构造 QoS0 PUBLISH");
    if (pub) {
        XMqttServer_feedData((XMqttServer*)mock, T2,
                             (const uint8_t*)XByteArray_constData(pub),
                             (size_t)XByteArray_size_base(pub));
        XByteArray_delete_base(pub);
    }
    if (mock_find_packet_header(mock_output(mock, T1), 0x30, &header, &body)) {
        const uint8_t* bytes = (const uint8_t*)XByteArray_constData(body);
        size_t size = (size_t)XByteArray_size_base(body);
        size_t topicSize = ((size_t)bytes[0] << 8) | bytes[1];
        MOCK_CHECK((header & 0x06U) == 0x00U && topicSize == 11 &&
                   size >= 2 + topicSize &&
                   memcmp(bytes + 2, "topic/hello", 11) == 0 &&
                   size == 2 + topicSize + sizeof(payload) - 1 &&
                   memcmp(bytes + 2 + topicSize, payload, sizeof(payload) - 1) == 0,
                   "QoS0 PUBLISH 主题与载荷正确");
        XByteArray_delete_base(body);
        body = NULL;
        header = 0;
    } else {
        MOCK_CHECK(false, "订阅者收到 QoS0 PUBLISH");
    }
    mock_server_delete(mock);
}

/* 3. PUBLISH QoS1：发布者 PUBACK + 订阅者 QoS1 投递 */
static void mock_test_publish_qos1(void)
{
    XMqttServerMock* mock;
    XByteArray* sub;
    XByteArray* pub;
    XByteArray* body = NULL;
    uint8_t header = 0;
    static const uint8_t payload[] = "hello-qos1";
    MockSubscribeItem item = {"q1/#", 1};
    XPrintf("---- PUBLISH QoS1 ----\n");

    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    MOCK_CHECK(mock_connect_existing(mock, T1, "sub-q1", true, 0), "订阅者 CONNECT 成功");
    MOCK_CHECK(mock_connect_existing(mock, T2, "pub-q1", true, 0), "发布者 CONNECT 成功");

    sub = mock_subscribe(1, &item, 1);
    MOCK_CHECK(sub != NULL, "构造 SUBSCRIBE q1/#");
    if (sub) {
        XMqttServer_feedData((XMqttServer*)mock, T1,
                             (const uint8_t*)XByteArray_constData(sub),
                             (size_t)XByteArray_size_base(sub));
        XByteArray_delete_base(sub);
    }
    mock_clear_output(mock, T1);

    pub = mock_publish("q1/msg", payload, sizeof(payload) - 1, 1, false, 0x1234);
    MOCK_CHECK(pub != NULL, "构造 QoS1 PUBLISH");
    if (pub) {
        XMqttServer_feedData((XMqttServer*)mock, T2,
                             (const uint8_t*)XByteArray_constData(pub),
                             (size_t)XByteArray_size_base(pub));
        XByteArray_delete_base(pub);
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T2), 0x40, &body),
               "发布者收到 PUBACK");
    if (body) {
        MOCK_CHECK(mock_body_u16(body, 0) == 0x1234, "PUBACK 报文标识符正确");
        XByteArray_delete_base(body);
        body = NULL;
    }
    if (mock_find_packet_header(mock_output(mock, T1), 0x30, &header, &body)) {
        const uint8_t* bytes = (const uint8_t*)XByteArray_constData(body);
        size_t size = (size_t)XByteArray_size_base(body);
        size_t topicSize = ((size_t)bytes[0] << 8) | bytes[1];
        MOCK_CHECK(size >= 2 + topicSize + 2 &&
                   (header & 0x06U) == 0x02U &&
                   memcmp(bytes + 2, "q1/msg", 6) == 0 &&
                   mock_body_u16(body, 2 + topicSize) > 0,
                   "QoS1 PUBLISH 标头/主题/报文标识符正确");
        XByteArray_delete_base(body);
        body = NULL;
        header = 0;
        /* 订阅者回 PUBACK，验证后不再有 PUBREL */
        {
            XByteArray* puback = mock_ack(0x40, 1);
            if (puback) {
                XMqttServer_feedData((XMqttServer*)mock, T1,
                                     (const uint8_t*)XByteArray_constData(puback),
                                     (size_t)XByteArray_size_base(puback));
                XByteArray_delete_base(puback);
            }
        }
        mock_clear_output(mock, T1);
        XMqttServer_publish((XMqttServer*)mock, "q1/other",
                            (const uint8_t*)"x", 1, 1, false);
        MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x30, &body) &&
                       body && mock_body_u16(body, 2 + 8) > 0,
                   "PUBACK 后会话队列清空，新 QoS1 可继续投递");
        if (body) XByteArray_delete_base(body);
    }
    mock_server_delete(mock);
}

/* 4. PUBLISH QoS2：完整四次握手 + 投递 */
static void mock_test_publish_qos2(void)
{
    XMqttServerMock* mock;
    XByteArray* sub;
    XByteArray* pub;
    XByteArray* body = NULL;
    uint8_t header = 0;
    static const uint8_t payload[] = "hello-qos2";
    MockSubscribeItem item = {"q2/#", 2};
    XPrintf("---- PUBLISH QoS2 四次握手 ----\n");

    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    MOCK_CHECK(mock_connect_existing(mock, T1, "sub-q2", true, 0), "订阅者 CONNECT 成功");
    MOCK_CHECK(mock_connect_existing(mock, T2, "pub-q2", true, 0), "发布者 CONNECT 成功");

    sub = mock_subscribe(1, &item, 1);
    MOCK_CHECK(sub != NULL, "构造 SUBSCRIBE q2/#");
    if (sub) {
        XMqttServer_feedData((XMqttServer*)mock, T1,
                             (const uint8_t*)XByteArray_constData(sub),
                             (size_t)XByteArray_size_base(sub));
        XByteArray_delete_base(sub);
    }
    mock_clear_output(mock, T1);

    pub = mock_publish("q2/msg", payload, sizeof(payload) - 1, 2, false, 0x5678);
    MOCK_CHECK(pub != NULL, "构造 QoS2 PUBLISH");
    if (pub) {
        XMqttServer_feedData((XMqttServer*)mock, T2,
                             (const uint8_t*)XByteArray_constData(pub),
                             (size_t)XByteArray_size_base(pub));
        XByteArray_delete_base(pub);
    }
    /* 发布者：PUBLISH -> PUBREC */
    MOCK_CHECK(mock_find_packet(mock_output(mock, T2), 0x50, &body) && body &&
                   mock_body_u16(body, 0) == 0x5678,
               "发布者收到 PUBREC（含报文标识符）");
    if (body) XByteArray_delete_base(body);
    /* 发布者回 PUBREL */
    {
        XByteArray* pubrel = mock_ack(0x62, 0x5678);
        MOCK_CHECK(pubrel != NULL, "构造 PUBREL");
        if (pubrel) {
            XMqttServer_feedData((XMqttServer*)mock, T2,
                                 (const uint8_t*)XByteArray_constData(pubrel),
                                 (size_t)XByteArray_size_base(pubrel));
            XByteArray_delete_base(pubrel);
        }
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T2), 0x70, &body) && body &&
                   mock_body_u16(body, 0) == 0x5678,
               "发布者收到 PUBCOMP");
    if (body) XByteArray_delete_base(body);
    /* 订阅者收到 QoS2 PUBLISH */
    if (mock_find_packet_header(mock_output(mock, T1), 0x30, &header, &body)) {
        uint16_t deliveredId = mock_body_u16(body, 2 + 6);
        MOCK_CHECK((header & 0x06U) == 0x04U &&
                       deliveredId > 0,
                   "投递 QoS2 PUBLISH 标头与报文标识符正确");
        XByteArray_delete_base(body);
        body = NULL;
        header = 0;
        /* 订阅者回 PUBREC -> 服务器回 PUBREL -> 订阅者回 PUBCOMP */
        {
            XByteArray* pubrec = mock_ack(0x50, deliveredId);
            XByteArray* pubrel = NULL;
            if (pubrec) {
                XMqttServer_feedData((XMqttServer*)mock, T1,
                                     (const uint8_t*)XByteArray_constData(pubrec),
                                     (size_t)XByteArray_size_base(pubrec));
                XByteArray_delete_base(pubrec);
            }
            MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x60, &pubrel) &&
                       pubrel && mock_body_u16(pubrel, 0) == deliveredId,
                       "服务器收到 PUBREC 后回 PUBREL");
            if (pubrel) XByteArray_delete_base(pubrel);
            mock_clear_output(mock, T1);
            {
                XByteArray* pubcomp = mock_ack(0x70, deliveredId);
                if (pubcomp) {
                    XMqttServer_feedData((XMqttServer*)mock, T1,
                                         (const uint8_t*)XByteArray_constData(pubcomp),
                                         (size_t)XByteArray_size_base(pubcomp));
                    XByteArray_delete_base(pubcomp);
                }
            }
            /* 投递新 QoS2 验证队列已清空 */
            XMqttServer_publish((XMqttServer*)mock, "q2/next",
                                (const uint8_t*)"y", 1, 2, false);
            MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x30, &body),
                       "PUBCOMP 后队列清空，新 QoS2 可继续投递");
            if (body) XByteArray_delete_base(body);
        }
    }
    mock_server_delete(mock);
}

/* 5. 保留消息 */
static void mock_test_retained(void)
{
    XMqttServerMock* mock;
    XByteArray* pub;
    XByteArray* body = NULL;
    uint8_t header = 0;
    static const uint8_t payload[] = "keep-me";
    MockSubscribeItem item = {"ret/#", 1};
    XPrintf("---- 保留消息 ----\n");

    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    MOCK_CHECK(mock_connect_existing(mock, T1, "pub-ret", true, 0), "发布者 CONNECT 成功");
    pub = mock_publish("ret/state", payload, sizeof(payload) - 1, 1, true, 0x1001);
    MOCK_CHECK(pub != NULL, "构造保留 PUBLISH");
    if (pub) {
        XMqttServer_feedData((XMqttServer*)mock, T1,
                             (const uint8_t*)XByteArray_constData(pub),
                             (size_t)XByteArray_size_base(pub));
        XByteArray_delete_base(pub);
    }
    mock_clear_output(mock, T1);
    XMqttServer_endClient((XMqttServer*)mock, T1);
    mock_clear_output(mock, T1);

    /* 新订阅者连入并订阅 ret/#，应立即收到保留消息 */
    MOCK_CHECK(mock_connect_existing(mock, T1, "sub-ret", true, 0), "新订阅者 CONNECT 成功");
    {
        XByteArray* sub = mock_subscribe(1, &item, 1);
        MOCK_CHECK(sub != NULL, "构造 SUBSCRIBE ret/#");
        if (sub) {
            XMqttServer_feedData((XMqttServer*)mock, T1,
                                 (const uint8_t*)XByteArray_constData(sub),
                                 (size_t)XByteArray_size_base(sub));
            XByteArray_delete_base(sub);
        }
    }
    if (mock_find_packet_header(mock_output(mock, T1), 0x30, &header, &body)) {
        const uint8_t* bytes = (const uint8_t*)XByteArray_constData(body);
        size_t size = (size_t)XByteArray_size_base(body);
        size_t topicSize = ((size_t)bytes[0] << 8) | bytes[1];
        MOCK_CHECK((header & 0x01U) != 0 && topicSize == 9 &&
                   size >= 2 + topicSize &&
                   memcmp(bytes + 2, "ret/state", 9) == 0,
                   "保留消息带 RETAIN 标志且主题正确");
        XByteArray_delete_base(body);
        body = NULL;
        header = 0;
    } else {
        MOCK_CHECK(false, "订阅保留消息主题后立即收到保留消息");
    }
    /* 空载荷删除保留消息 */
    mock_clear_output(mock, T1);
    {
        XByteArray* clear = mock_publish("ret/state", NULL, 0, 0, true, 0);
        if (clear) {
            XMqttServer_feedData((XMqttServer*)mock, T1,
                                 (const uint8_t*)XByteArray_constData(clear),
                                 (size_t)XByteArray_size_base(clear));
            XByteArray_delete_base(clear);
        }
    }
    mock_clear_output(mock, T1);
    XMqttServer_endClient((XMqttServer*)mock, T1);
    MOCK_CHECK(mock_connect_existing(mock, T1, "sub-ret2", true, 0), "第三个订阅者 CONNECT 成功");
    {
        XByteArray* sub = mock_subscribe(1, &item, 1);
        if (sub) {
            XMqttServer_feedData((XMqttServer*)mock, T1,
                                 (const uint8_t*)XByteArray_constData(sub),
                                 (size_t)XByteArray_size_base(sub));
            XByteArray_delete_base(sub);
        }
    }
    MOCK_CHECK(mock_count_packets(mock_output(mock, T1), 0x30) == 0,
               "空载荷删除保留消息后不再投递");
    mock_server_delete(mock);
}

/* 6. UNSUBSCRIBE */
static void mock_test_unsubscribe(void)
{
    XMqttServerMock* mock;
    XByteArray* sub;
    XByteArray* body = NULL;
    static const uint8_t payload[] = "after-unsub";
    MockSubscribeItem item = {"u/#", 0};
    const char* filters[] = {"u/#"};
    XPrintf("---- UNSUBSCRIBE ----\n");

    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    MOCK_CHECK(mock_connect_existing(mock, T1, "unsub-1", true, 0), "订阅者 CONNECT 成功");
    sub = mock_subscribe(1, &item, 1);
    MOCK_CHECK(sub != NULL, "构造 SUBSCRIBE u/#");
    if (sub) {
        XMqttServer_feedData((XMqttServer*)mock, T1,
                             (const uint8_t*)XByteArray_constData(sub),
                             (size_t)XByteArray_size_base(sub));
        XByteArray_delete_base(sub);
    }
    mock_clear_output(mock, T1);
    {
        XByteArray* unsub = mock_unsubscribe(2, filters, 1);
        MOCK_CHECK(unsub != NULL, "构造 UNSUBSCRIBE");
        if (unsub) {
            XMqttServer_feedData((XMqttServer*)mock, T1,
                                 (const uint8_t*)XByteArray_constData(unsub),
                                 (size_t)XByteArray_size_base(unsub));
            XByteArray_delete_base(unsub);
        }
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0xB0, &body),
               "收到 UNSUBACK");
    if (body) XByteArray_delete_base(body);
    mock_clear_output(mock, T1);
    XMqttServer_publish((XMqttServer*)mock, "u/x", payload, sizeof(payload) - 1, 0, false);
    MOCK_CHECK(mock_count_packets(mock_output(mock, T1), 0x30) == 0,
               "取消订阅后不再收到消息");
    mock_server_delete(mock);
}

/* 7. PINGREQ/PINGRESP */
static void mock_test_ping(void)
{
    XMqttServerMock* mock;
    XByteArray* ping;
    XByteArray* body = NULL;
    XPrintf("---- PINGREQ/PINGRESP ----\n");
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    MOCK_CHECK(mock_connect_existing(mock, T1, "ping-1", true, 30), "客户端 CONNECT 成功");
    ping = mock_pingreq();
    MOCK_CHECK(ping != NULL, "构造 PINGREQ");
    if (ping) {
        XMqttServer_feedData((XMqttServer*)mock, T1,
                             (const uint8_t*)XByteArray_constData(ping),
                             (size_t)XByteArray_size_base(ping));
        XByteArray_delete_base(ping);
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0xD0, &body),
               "收到 PINGRESP");
    if (body) XByteArray_delete_base(body);
    mock_server_delete(mock);
}

/* 8. 遗嘱消息（异常断开） */
static void mock_test_will(void)
{
    XMqttServerMock* mock;
    XByteArray* connect;
    XByteArray* sub;
    XByteArray* body = NULL;
    MockSubscribeItem item = {"will/#", 0};
    XPrintf("---- 遗嘱消息 ----\n");

    /* 订阅者 */
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    MOCK_CHECK(mock_connect_existing(mock, T1, "will-sub", true, 0), "订阅者 CONNECT 成功");
    sub = mock_subscribe(1, &item, 1);
    MOCK_CHECK(sub != NULL, "构造 SUBSCRIBE will/#");
    if (sub) {
        XMqttServer_feedData((XMqttServer*)mock, T1,
                             (const uint8_t*)XByteArray_constData(sub),
                             (size_t)XByteArray_size_base(sub));
        XByteArray_delete_base(sub);
    }
    mock_clear_output(mock, T1);

    /* 带遗嘱的客户端（v4 CONNECT 带遗嘱标志） */
    {
        XByteArray* body_buf = XByteArray_create();
        XByteArray* packet;
        if (body_buf) {
            if (mock_append_string(body_buf, "MQTT") &&
                mock_append_u8(body_buf, 4) &&
                mock_append_u8(body_buf, 0x04 | (0x01U << 3)) &&
                mock_append_u16(body_buf, 0) &&
                mock_append_string(body_buf, "will-client") &&
                mock_append_string(body_buf, "will/topic") &&
                mock_append_binary(body_buf, (const uint8_t*)"gone", 4)) {
                packet = mock_packet(0x10, body_buf);
                if (packet) {
                    XMqttServer_beginClient((XMqttServer*)mock, T2);
                    XMqttServer_feedData((XMqttServer*)mock, T2,
                                         (const uint8_t*)XByteArray_constData(packet),
                                         (size_t)XByteArray_size_base(packet));
                    XByteArray_delete_base(packet);
                }
            }
            XByteArray_delete_base(body_buf);
        }
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T2), 0x20, &body),
               "带遗嘱客户端 CONNECT 成功");
    if (body) XByteArray_delete_base(body);
    mock_clear_output(mock, T2);

    /* 异常断开（未发 DISCONNECT）触发遗嘱 */
    XMqttServer_endClient((XMqttServer*)mock, T2);
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x30, &body),
               "订阅者收到遗嘱消息");
    if (body) {
        const uint8_t* bytes = (const uint8_t*)XByteArray_constData(body);
        size_t size = (size_t)XByteArray_size_base(body);
        size_t topicSize = ((size_t)bytes[0] << 8) | bytes[1];
        MOCK_CHECK(topicSize == 10 && size >= 2 + topicSize &&
                   memcmp(bytes + 2, "will/topic", 10) == 0 &&
                   size == 2 + topicSize + 4 &&
                   memcmp(bytes + 2 + topicSize, "gone", 4) == 0,
                   "遗嘱主题与载荷正确");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_server_delete(mock);
}

/* 9. 正常 DISCONNECT 不触发遗嘱 */
static void mock_test_disconnect_clean(void)
{
    XMqttServerMock* mock;
    XByteArray* body = NULL;
    MockSubscribeItem item = {"will/#", 0};
    XPrintf("---- 正常 DISCONNECT ----\n");

    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    MOCK_CHECK(mock_connect_existing(mock, T1, "clean-will-sub", true, 0), "订阅者 CONNECT 成功");
    {
        XByteArray* sub = mock_subscribe(1, &item, 1);
        if (sub) {
            XMqttServer_feedData((XMqttServer*)mock, T1,
                                 (const uint8_t*)XByteArray_constData(sub),
                                 (size_t)XByteArray_size_base(sub));
            XByteArray_delete_base(sub);
        }
    }
    mock_clear_output(mock, T1);
    {
        XByteArray* body_buf = XByteArray_create();
        XByteArray* packet;
        if (body_buf) {
            if (mock_append_string(body_buf, "MQTT") &&
                mock_append_u8(body_buf, 4) &&
                mock_append_u8(body_buf, 0x04 | (0x01U << 3)) &&
                mock_append_u16(body_buf, 0) &&
                mock_append_string(body_buf, "clean-will") &&
                mock_append_string(body_buf, "will/topic") &&
                mock_append_binary(body_buf, (const uint8_t*)"gone", 4)) {
                packet = mock_packet(0x10, body_buf);
                if (packet) {
                    XMqttServer_beginClient((XMqttServer*)mock, T2);
                    XMqttServer_feedData((XMqttServer*)mock, T2,
                                         (const uint8_t*)XByteArray_constData(packet),
                                         (size_t)XByteArray_size_base(packet));
                    XByteArray_delete_base(packet);
                }
            }
            XByteArray_delete_base(body_buf);
        }
    }
    mock_clear_output(mock, T2);
    {
        XByteArray* disc = mock_disconnect();
        if (disc) {
            XMqttServer_feedData((XMqttServer*)mock, T2,
                                 (const uint8_t*)XByteArray_constData(disc),
                                 (size_t)XByteArray_size_base(disc));
            XByteArray_delete_base(disc);
        }
    }
    XMqttServer_endClient((XMqttServer*)mock, T2);
    MOCK_CHECK(mock_count_packets(mock_output(mock, T1), 0x30) == 0,
               "正常 DISCONNECT 不触发遗嘱");
    mock_server_delete(mock);
}

/* 10. 认证回调 */
static uint8_t mock_auth_cb(void* ctx, const char* username, const char* password)
{
    int* calls = (int*)ctx;
    if (calls) ++*calls;
    if (username && strcmp(username, "admin") == 0 &&
        password && strcmp(password, "secret") == 0) return 0;
    return 0x87; /* XMqtt_ReasonCode_NotAuthorized */
}

static void mock_test_auth(void)
{
    XMqttServerMock* mock;
    XByteArray* body = NULL;
    int calls = 0;
    XPrintf("---- 认证回调 ----\n");

    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    XMqttServer_setAuthenticator((XMqttServer*)mock, &calls, mock_auth_cb);
    XMqttServer_beginClient((XMqttServer*)mock, T1);
    /* 错误密码 */
    {
        XByteArray* body_buf = XByteArray_create();
        XByteArray* packet;
        if (body_buf) {
            if (mock_append_string(body_buf, "MQTT") &&
                mock_append_u8(body_buf, 4) &&
                mock_append_u8(body_buf, 0xC2) &&
                mock_append_u16(body_buf, 0) &&
                mock_append_string(body_buf, "auth-1") &&
                mock_append_string(body_buf, "admin") &&
                mock_append_binary(body_buf, (const uint8_t*)"wrong", 5)) {
                packet = mock_packet(0x10, body_buf);
                if (packet) {
                    XMqttServer_feedData((XMqttServer*)mock, T1,
                                         (const uint8_t*)XByteArray_constData(packet),
                                         (size_t)XByteArray_size_base(packet));
                    XByteArray_delete_base(packet);
                }
            }
            XByteArray_delete_base(body_buf);
        }
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x20, &body) && body &&
                   XByteArray_size_base(body) >= 2 && XByteArray_constData(body)[1] != 0,
               "认证失败返回非成功 CONNACK");
    if (body) XByteArray_delete_base(body);
    MOCK_CHECK(mock->closeCount > 0, "认证失败后传输被关闭");
    mock_clear_output(mock, T1);

    /* 正确凭据 */
    XMqttServer_beginClient((XMqttServer*)mock, T2);
    {
        XByteArray* body_buf = XByteArray_create();
        XByteArray* packet;
        if (body_buf) {
            if (mock_append_string(body_buf, "MQTT") &&
                mock_append_u8(body_buf, 4) &&
                mock_append_u8(body_buf, 0xC2) &&
                mock_append_u16(body_buf, 0) &&
                mock_append_string(body_buf, "auth-2") &&
                mock_append_string(body_buf, "admin") &&
                mock_append_binary(body_buf, (const uint8_t*)"secret", 6)) {
                packet = mock_packet(0x10, body_buf);
                if (packet) {
                    XMqttServer_feedData((XMqttServer*)mock, T2,
                                         (const uint8_t*)XByteArray_constData(packet),
                                         (size_t)XByteArray_size_base(packet));
                    XByteArray_delete_base(packet);
                }
            }
            XByteArray_delete_base(body_buf);
        }
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T2), 0x20, &body) && body &&
                   XByteArray_size_base(body) >= 2 && XByteArray_constData(body)[1] == 0,
               "认证成功返回成功 CONNACK");
    if (body) XByteArray_delete_base(body);
    MOCK_CHECK(calls >= 2, "认证回调被调用");
    mock_server_delete(mock);
}

/* 11. 服务端主动发布 */
static void mock_test_server_publish(void)
{
    XMqttServerMock* mock;
    XByteArray* sub;
    XByteArray* body = NULL;
    static const uint8_t payload[] = "from-server";
    MockSubscribeItem item = {"srv/#", 1};
    XPrintf("---- 服务端主动发布 ----\n");

    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    MOCK_CHECK(mock_connect_existing(mock, T1, "srv-sub", true, 0), "订阅者 CONNECT 成功");
    sub = mock_subscribe(1, &item, 1);
    MOCK_CHECK(sub != NULL, "构造 SUBSCRIBE srv/#");
    if (sub) {
        XMqttServer_feedData((XMqttServer*)mock, T1,
                             (const uint8_t*)XByteArray_constData(sub),
                             (size_t)XByteArray_size_base(sub));
        XByteArray_delete_base(sub);
    }
    mock_clear_output(mock, T1);
    MOCK_CHECK(XMqttServer_publish((XMqttServer*)mock, "srv/hello",
                                   payload, sizeof(payload) - 1, 1, false),
               "XMqttServer_publish 返回 true");
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x30, &body),
               "订阅者收到服务端发布消息");
    if (body) {
        const uint8_t* bytes = (const uint8_t*)XByteArray_constData(body);
        size_t size = (size_t)XByteArray_size_base(body);
        size_t topicSize = ((size_t)bytes[0] << 8) | bytes[1];
        MOCK_CHECK(topicSize == 9 && size >= 2 + topicSize &&
                   memcmp(bytes + 2, "srv/hello", 9) == 0,
                   "服务端发布主题正确");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_server_delete(mock);
}

/* 12. 持久会话 + 离线 QoS1 队列 */
static void mock_test_persistent_session(void)
{
    XMqttServerMock* mock;
    XByteArray* sub;
    XByteArray* body = NULL;
    MockSubscribeItem item = {"off/#", 1};
    XPrintf("---- 持久会话与离线队列 ----\n");

    /* 第一次连接：clean=false，订阅 */
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    MOCK_CHECK(mock_connect_existing(mock, T1, "persist-1", false, 0), "持久会话客户端 CONNECT 成功");
    sub = mock_subscribe(1, &item, 1);
    MOCK_CHECK(sub != NULL, "构造 SUBSCRIBE off/#");
    if (sub) {
        XMqttServer_feedData((XMqttServer*)mock, T1,
                             (const uint8_t*)XByteArray_constData(sub),
                             (size_t)XByteArray_size_base(sub));
        XByteArray_delete_base(sub);
    }
    mock_clear_output(mock, T1);
    XMqttServer_endClient((XMqttServer*)mock, T1);

    /* 离线期间发布 QoS1 消息 */
    XMqttServer_publish((XMqttServer*)mock, "off/queued",
                        (const uint8_t*)"queued", 6, 1, false);

    /* 重连（clean=false）：应恢复会话并投递离线消息 */
    MOCK_CHECK(mock_connect_existing_ex(mock, T2, "persist-1", false, 0, false),
               "持久会话客户端重连成功");
    MOCK_CHECK(mock_find_packet(mock_output(mock, T2), 0x20, &body) && body &&
                   XByteArray_size_base(body) >= 1 && (XByteArray_constData(body)[0] & 0x01U) != 0,
               "重连时 CONNACK 会话已恢复（sessionPresent=1）");
    if (body) XByteArray_delete_base(body);
    MOCK_CHECK(mock_find_packet(mock_output(mock, T2), 0x30, &body),
               "重连后收到离线队列消息");
    if (body) {
        const uint8_t* bytes = (const uint8_t*)XByteArray_constData(body);
        size_t size = (size_t)XByteArray_size_base(body);
        size_t topicSize = ((size_t)bytes[0] << 8) | bytes[1];
        MOCK_CHECK(topicSize == 10 && size >= 2 + topicSize + 2 &&
                   memcmp(bytes + 2, "off/queued", 10) == 0,
                   "离线队列消息主题正确");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_server_delete(mock);
}

/* 13. 共享订阅 */
static void mock_test_shared_subscription(void)
{
    XMqttServerMock* mock;
    XByteArray* sub;
    XByteArray* body = NULL;
    MockSubscribeItem item = {"$share/group/sh/#", 0};
    XPrintf("---- 共享订阅 ----\n");

    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    MOCK_CHECK(mock_connect_existing(mock, T1, "shared-1", true, 0), "共享订阅者1 CONNECT 成功");
    sub = mock_subscribe(1, &item, 1);
    MOCK_CHECK(sub != NULL, "构造共享 SUBSCRIBE");
    if (sub) {
        XMqttServer_feedData((XMqttServer*)mock, T1,
                             (const uint8_t*)XByteArray_constData(sub),
                             (size_t)XByteArray_size_base(sub));
        XByteArray_delete_base(sub);
    }
    mock_clear_output(mock, T1);
    MOCK_CHECK(mock_connect_existing(mock, T2, "shared-2", true, 0), "共享订阅者2 CONNECT 成功");
    sub = mock_subscribe(1, &item, 1);
    MOCK_CHECK(sub != NULL, "构造共享 SUBSCRIBE（第二个成员）");
    if (sub) {
        XMqttServer_feedData((XMqttServer*)mock, T2,
                             (const uint8_t*)XByteArray_constData(sub),
                             (size_t)XByteArray_size_base(sub));
        XByteArray_delete_base(sub);
    }
    mock_clear_output(mock, T2);

    /* 发布 4 条消息：两条各投递到不同成员（轮转） */
    {
        int i;
        for (i = 0; i < 4; ++i) {
            char topic[32];
            static const uint8_t one = '1';
            snprintf(topic, sizeof(topic), "sh/msg%d", i);
            XMqttServer_publish((XMqttServer*)mock, topic, &one, 1, 0, false);
        }
    }
    MOCK_CHECK(mock_count_packets(mock_output(mock, T1), 0x30) == 2 &&
               mock_count_packets(mock_output(mock, T2), 0x30) == 2,
               "共享订阅 4 条消息按成员轮转各投递 2 条");
    mock_server_delete(mock);
}

/* 14. 主题别名（MQTT 5.0 入站） */
static void mock_test_topic_alias(void)
{
    XMqttServerMock* mock;
    XByteArray* body = NULL;
    MockSubscribeItem item = {"alias/#", 0};
    XPrintf("---- MQTT 5.0 主题别名 ----\n");

    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    XMqttServer_setTopicAliasMaximum((XMqttServer*)mock, 10);
    XMqttServer_beginClient((XMqttServer*)mock, T1);
    {
        XByteArray* connect = mock_connect_v5("alias-1", true, 0, 0);
        if (connect) {
            XMqttServer_feedData((XMqttServer*)mock, T1,
                                 (const uint8_t*)XByteArray_constData(connect),
                                 (size_t)XByteArray_size_base(connect));
            XByteArray_delete_base(connect);
        }
    }
    mock_clear_output(mock, T1);
    {
        XByteArray* sub = mock_subscribe_v5(1, &item, 1);
        if (sub) {
            XMqttServer_feedData((XMqttServer*)mock, T1,
                                 (const uint8_t*)XByteArray_constData(sub),
                                 (size_t)XByteArray_size_base(sub));
            XByteArray_delete_base(sub);
        }
    }
    mock_clear_output(mock, T1);
    /* 第一次发布：带主题 + 别名 3 */
    /* 属性块长度 = 1 字节属性 ID + 2 字节别名值 = 3 */

    {
        XByteArray* body_buf = XByteArray_create();
        XByteArray* packet;
        if (body_buf) {
            if (mock_append_string(body_buf, "alias/long") &&
                mock_append_u8(body_buf, 3) &&
                mock_append_u8(body_buf, 0x23) &&
                mock_append_u16(body_buf, 3) &&
                mock_append_bytes(body_buf, "payload", 7)) {
                packet = mock_packet(0x30, body_buf);
                if (packet) {
                    XMqttServer_feedData((XMqttServer*)mock, T1,
                                         (const uint8_t*)XByteArray_constData(packet),
                                         (size_t)XByteArray_size_base(packet));
                    XByteArray_delete_base(packet);
                }
            }
            XByteArray_delete_base(body_buf);
        }
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x30, &body),
               "首次发布（带别名）回显给订阅者");
    if (body) XByteArray_delete_base(body);
    mock_clear_output(mock, T1);
    /* 第二次发布：空主题 + 别名 3 */
    {
        XByteArray* body_buf = XByteArray_create();
        XByteArray* packet;
        if (body_buf) {
            if (mock_append_u16(body_buf, 0) &&
                mock_append_u8(body_buf, 3) &&
                mock_append_u8(body_buf, 0x23) &&
                mock_append_u16(body_buf, 3) &&
                mock_append_bytes(body_buf, "again", 5)) {
                packet = mock_packet(0x30, body_buf);
                if (packet) {
                    XMqttServer_feedData((XMqttServer*)mock, T1,
                                         (const uint8_t*)XByteArray_constData(packet),
                                         (size_t)XByteArray_size_base(packet));
                    XByteArray_delete_base(packet);
                }
            }
            XByteArray_delete_base(body_buf);
        }
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x30, &body),
               "空主题复用别名后仍正确路由");
    if (body) {
        const uint8_t* bytes = (const uint8_t*)XByteArray_constData(body);
        size_t size = (size_t)XByteArray_size_base(body);
        size_t topicSize = ((size_t)bytes[0] << 8) | bytes[1];
        MOCK_CHECK(topicSize == 10 && memcmp(bytes + 2, "alias/long", 10) == 0,
                   "别名解析回原主题");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_server_delete(mock);
}

/* 15. 同 clientId 抢占 */
static void mock_test_clientid_takeover(void)
{
    XMqttServerMock* mock;
    XByteArray* body = NULL;
    XPrintf("---- 同 clientId 抢占 ----\n");

    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    MOCK_CHECK(mock_connect_existing(mock, T1, "dup-client", true, 0), "旧连接 CONNECT 成功");
    mock_clear_output(mock, T1);
    MOCK_CHECK(mock_connect_existing_ex(mock, T2, "dup-client", true, 0, false),
               "新连接 CONNECT 成功");
    MOCK_CHECK(mock_find_packet(mock_output(mock, T2), 0x20, &body),
               "新连接 CONNACK 成功");
    if (body) XByteArray_delete_base(body);
    MOCK_CHECK(mock->closeCount >= 1, "旧连接被服务器关闭");
    mock_server_delete(mock);
}


/* 16. 配置接口全覆盖（getter/setter 与 NULL 安全） */
static void mock_test_config_api(void)
{
    XMqttServerMock* mock;
    XMqttServer* server;
    XPrintf("---- 配置接口全覆盖 ----\n");
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    server = (XMqttServer*)mock;

    /* 默认值 */
    MOCK_CHECK(XMqttServer_maximumPacketSize(server) == 268435455U,
               "默认最大报文大小 268435455");
    MOCK_CHECK(XMqttServer_topicAliasMaximum(server) == 0, "默认主题别名上限 0");
    MOCK_CHECK(XMqttServer_serverKeepAlive(server) == 0, "默认服务器保活 0（沿用客户端）");
    MOCK_CHECK(XMqttServer_maximumQoS(server) == 2, "默认最大 QoS 2");
    MOCK_CHECK(XMqttServer_retainAvailable(server), "默认支持保留消息");
    MOCK_CHECK(XMqttServer_wildcardAvailable(server), "默认支持通配符订阅");
    MOCK_CHECK(XMqttServer_subscriptionIdAvailable(server), "默认支持订阅标识符");
    MOCK_CHECK(XMqttServer_sharedAvailable(server), "默认支持共享订阅");

    /* 设置并读回 */
    XMqttServer_setMaximumPacketSize(server, 65536);
    MOCK_CHECK(XMqttServer_maximumPacketSize(server) == 65536,
               "设置最大报文大小 65536 读回");
    XMqttServer_setMaximumPacketSize(server, 0);
    MOCK_CHECK(XMqttServer_maximumPacketSize(server) == 268435455U,
               "设置为 0 恢复协议默认上限");
    XMqttServer_setTopicAliasMaximum(server, 123);
    MOCK_CHECK(XMqttServer_topicAliasMaximum(server) == 123, "设置主题别名上限 123 读回");
    XMqttServer_setServerKeepAlive(server, 45);
    MOCK_CHECK(XMqttServer_serverKeepAlive(server) == 45, "设置服务器保活 45 读回");
    XMqttServer_setMaximumQoS(server, 1);
    MOCK_CHECK(XMqttServer_maximumQoS(server) == 1, "设置最大 QoS 1 读回");
    XMqttServer_setMaximumQoS(server, 9);
    MOCK_CHECK(XMqttServer_maximumQoS(server) == 2, "最大 QoS 超范围钳制为 2");
    XMqttServer_setRetainAvailable(server, false);
    MOCK_CHECK(!XMqttServer_retainAvailable(server), "关闭保留消息读回");
    XMqttServer_setWildcardAvailable(server, false);
    MOCK_CHECK(!XMqttServer_wildcardAvailable(server), "关闭通配符订阅读回");
    XMqttServer_setSubscriptionIdAvailable(server, false);
    MOCK_CHECK(!XMqttServer_subscriptionIdAvailable(server), "关闭订阅标识符读回");
    XMqttServer_setSharedAvailable(server, false);
    MOCK_CHECK(!XMqttServer_sharedAvailable(server), "关闭共享订阅读回");

    /* 恢复默认并验证 */
    XMqttServer_setRetainAvailable(server, true);
    XMqttServer_setWildcardAvailable(server, true);
    XMqttServer_setSubscriptionIdAvailable(server, true);
    XMqttServer_setSharedAvailable(server, true);
    XMqttServer_setMaximumQoS(server, 2);
    MOCK_CHECK(XMqttServer_retainAvailable(server) && XMqttServer_wildcardAvailable(server) &&
               XMqttServer_subscriptionIdAvailable(server) && XMqttServer_sharedAvailable(server) &&
               XMqttServer_maximumQoS(server) == 2, "全部恢复默认值");

    /* NULL 安全 */
    MOCK_CHECK(XMqttServer_maximumPacketSize(NULL) == 0,
               "maximumPacketSize(NULL) 返回 0");
    MOCK_CHECK(XMqttServer_topicAliasMaximum(NULL) == 0,
               "topicAliasMaximum(NULL) 返回 0");
    MOCK_CHECK(XMqttServer_serverKeepAlive(NULL) == 0, "serverKeepAlive(NULL) 返回 0");
    MOCK_CHECK(XMqttServer_maximumQoS(NULL) == 0, "maximumQoS(NULL) 返回 0");
    MOCK_CHECK(!XMqttServer_retainAvailable(NULL), "retainAvailable(NULL) 返回 false");
    MOCK_CHECK(!XMqttServer_wildcardAvailable(NULL), "wildcardAvailable(NULL) 返回 false");
    MOCK_CHECK(!XMqttServer_subscriptionIdAvailable(NULL),
               "subscriptionIdAvailable(NULL) 返回 false");
    MOCK_CHECK(!XMqttServer_sharedAvailable(NULL), "sharedAvailable(NULL) 返回 false");
    XMqttServer_setMaximumPacketSize(NULL, 1);
    XMqttServer_setTopicAliasMaximum(NULL, 1);
    XMqttServer_setServerKeepAlive(NULL, 1);
    XMqttServer_setMaximumQoS(NULL, 1);
    XMqttServer_setRetainAvailable(NULL, false);
    XMqttServer_setWildcardAvailable(NULL, false);
    XMqttServer_setSubscriptionIdAvailable(NULL, false);
    XMqttServer_setSharedAvailable(NULL, false);
    XMqttServer_setAuthenticator(NULL, NULL, NULL);
    MOCK_CHECK(true, "setXxx(NULL) 均不崩溃");
    mock_server_delete(mock);
}

/* 17. 参数校验（NULL 安全） */
static void mock_test_null_safety(void)
{
    XMqttServerMock* mock;
    XMqttServer* server;
    XPrintf("---- 参数校验（NULL 安全） ----\n");
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    server = (XMqttServer*)mock;

    MOCK_CHECK(!XMqttServer_beginClient(NULL, T1), "beginClient(NULL 服务器) 返回 false");
    MOCK_CHECK(!XMqttServer_beginClient(server, NULL), "beginClient(NULL 传输) 返回 false");
    MOCK_CHECK(XMqttServer_beginClient(server, T1), "beginClient 正常登记");
    MOCK_CHECK(!XMqttServer_beginClient(server, T1), "相同传输重复登记返回 false");
    XMqttServer_endClient(NULL, T1);
    XMqttServer_endClient(server, NULL);
    XMqttServer_feedData(NULL, T1, NULL, 0);
    XMqttServer_feedData(server, NULL, NULL, 0);
    XMqttServer_feedData(server, T1, NULL, 1);
    MOCK_CHECK(!XMqttServer_publish(NULL, "a/b", NULL, 0, 0, false),
               "publish(NULL 服务器) 返回 false");
    MOCK_CHECK(!XMqttServer_publish(server, NULL, NULL, 0, 0, false),
               "publish(NULL 主题) 返回 false");
    MOCK_CHECK(!XMqttServer_publish(server, "a/+/b", (const uint8_t*)"x", 1, 0, false),
               "publish(含单级通配符主题) 返回 false");
    MOCK_CHECK(!XMqttServer_publish(server, "a/#", NULL, 0, 0, false),
               "publish(含多级通配符主题) 返回 false");
    MOCK_CHECK(!XMqttServer_publishWithProperties(NULL, "a/b", NULL, NULL, 0, 0, false),
               "publishWithProperties(NULL 服务器) 返回 false");
    MOCK_CHECK(!XMqttServer_publishWithProperties(server, NULL, NULL, NULL, 0, 0, false),
               "publishWithProperties(NULL 主题) 返回 false");
    MOCK_CHECK(!XMqttServer_sendData_base(NULL, T1, NULL, 0),
               "sendData_base(NULL 服务器) 返回 false");
    XMqttServer_closeClient_base(NULL, T1);
    XMqttServer_clientConnected_signal(NULL, T1);
    XMqttServer_clientDisconnected_signal(NULL, T1);
    XMqttServer_messageReceived_signal(NULL, T1, NULL, NULL);
    MOCK_CHECK(true, "全部 NULL 入参不崩溃");
    mock_server_delete(mock);
}

/* 18. 基类默认虚函数行为 */
static void mock_test_base_virtuals(void)
{
    XMqttServer* server;
    XPrintf("---- 基类默认虚函数 ----\n");
    server = XMqttServer_create();
    MOCK_CHECK(server != NULL, "创建基类 XMqttServer 实例");
    if (!server) return;
    MOCK_CHECK(!XMqttServer_sendData_base(server, T1, (const uint8_t*)"x", 1),
               "基类 sendData_base 默认返回 false");
    XMqttServer_closeClient_base(server, T1);
    XMqttServer_publish(server, "a/b", (const uint8_t*)"x", 1, 0, false);
    MOCK_CHECK(true, "基类 closeClient_base/publish 不崩溃");
    XClass_delete_base((XClass*)server);
}

/* 19. 服务端主动发布（带 MQTT 5.0 属性） */
static void mock_test_publish_with_properties(void)
{
    XMqttServerMock* mock;
    XMqttServer* server;
    XMqttPublishProperties* props = NULL;
    XByteArray* body = NULL;
    XByteArray* sub;
    MockSubscribeItem item = {"props/#", 0};
    static const uint8_t payload[] = "props-payload";
    XPrintf("---- 服务端主动发布（带 MQTT 5.0 属性） ----\n");
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    server = (XMqttServer*)mock;
    MOCK_CHECK(mock_connect_v5_existing_ex(mock, T1, "props-1", 0, true),
               "v5 订阅者 CONNECT 成功");
    sub = mock_subscribe_v5(1, &item, 1);
    MOCK_CHECK(sub != NULL, "构造 v5 SUBSCRIBE props/#");
    if (sub) {
        XMqttServer_feedData(server, T1, (const uint8_t*)XByteArray_constData(sub),
                             (size_t)XByteArray_size_base(sub));
        XByteArray_delete_base(sub);
    }
    mock_clear_output(mock, T1);

    props = XMqttPublishProperties_create();
    MOCK_CHECK(props != NULL, "创建发布属性");
    if (props) {
        XMqttPublishProperties_setContentType(props, "text/plain");
        XMqttPublishProperties_setPayloadFormatIndicator(props, 1);
        XMqttPublishProperties_setMessageExpiryInterval(props, 3600);
        MOCK_CHECK(XMqttServer_publishWithProperties(server, "props/hello", props,
                                                     payload, sizeof(payload) - 1, 0, false),
                   "publishWithProperties 返回 true");
        MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x30, &body),
                   "订阅者收到 PUBLISH");
        if (body) {
            MOCK_CHECK(mock_body_contains(body, (const uint8_t*)"\x03\x00\x0Atext/plain", 13),
                       "PUBLISH 携带 ContentType 属性");
            MOCK_CHECK(mock_body_contains(body, (const uint8_t*)"\x01\x01", 2),
                       "PUBLISH 携带载荷格式属性");
            MOCK_CHECK(mock_body_contains(body, (const uint8_t*)"\x02\x00\x00\x0E\x10", 5),
                       "PUBLISH 携带消息过期属性");
            XByteArray_delete_base(body);
            body = NULL;
        }
        XMqttPublishProperties_delete_base(props);
        props = NULL;
    }
    mock_clear_output(mock, T1);
    MOCK_CHECK(XMqttServer_publishWithProperties(server, "props/plain", NULL,
                                                 payload, sizeof(payload) - 1, 0, false),
               "publishWithProperties(属性为 NULL) 返回 true");
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x30, &body),
               "属性为 NULL 时订阅者仍收到 PUBLISH");
    if (body) XByteArray_delete_base(body);
    mock_server_delete(mock);
}

/* 20. 信号覆盖（clientConnected / clientDisconnected / messageReceived） */
static int g_sigConnectedCount;
static int g_sigDisconnectedCount;
static int g_sigReceivedCount;
static char g_sigReceivedTopic[64];
static uint8_t g_sigReceivedPayload[64];
static size_t g_sigReceivedPayloadSize;
static void* g_sigReceivedTransport;

static void mock_on_client_connected(XObject* receiver, XVarList* args)
{
    void* transport;
    (void)receiver;
    XVarList_args_1(args, void*, transportArg);
    transport = transportArg;
    if (transport == T1) ++g_sigConnectedCount;
}

static void mock_on_client_disconnected(XObject* receiver, XVarList* args)
{
    void* transport;
    (void)receiver;
    XVarList_args_1(args, void*, transportArg);
    transport = transportArg;
    if (transport == T1) ++g_sigDisconnectedCount;
}

static void mock_on_message_received(XObject* receiver, XVarList* args)
{
    const XMqttTopicName* topic;
    const XByteArray* payload;
    (void)receiver;
    XVarList_args_3(args, void*, transportArg, XMqttTopicName*, topicArg,
                    XByteArray*, payloadArg);
    g_sigReceivedTransport = transportArg;
    topic = (const XMqttTopicName*)topicArg;
    payload = (XByteArray*)payloadArg;
    ++g_sigReceivedCount;
    g_sigReceivedPayloadSize = payload ? (size_t)XByteArray_size_base(payload) : 0;
    if (g_sigReceivedPayloadSize > sizeof(g_sigReceivedPayload))
        g_sigReceivedPayloadSize = sizeof(g_sigReceivedPayload);
    if (g_sigReceivedPayloadSize)
        memcpy(g_sigReceivedPayload, XByteArray_constData((XByteArray*)payload),
               g_sigReceivedPayloadSize);
    g_sigReceivedTopic[0] = '\0';
    if (topic) {
        const XString* name = XMqttTopicName_name_const(topic);
        size_t len = name ? XString_toUtf8_length(name) : 0;
        if (len >= sizeof(g_sigReceivedTopic)) len = sizeof(g_sigReceivedTopic) - 1;
        if (len) memcpy(g_sigReceivedTopic, XString_toUtf8(name), len);
        g_sigReceivedTopic[len] = '\0';
    }
}

static void mock_test_signals(void)
{
    XMqttServerMock* mock;
    XMqttServer* server;
    XByteArray* packet;
    static const uint8_t payload[] = "hello-sig";
    XPrintf("---- clientConnected/clientDisconnected/messageReceived 信号 ----\n");
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器");
    if (!mock) return;
    server = (XMqttServer*)mock;
    g_sigConnectedCount = 0;
    g_sigDisconnectedCount = 0;
    g_sigReceivedCount = 0;
    g_sigReceivedPayloadSize = 0;
    g_sigReceivedTransport = NULL;
    g_sigReceivedTopic[0] = '\0';
    XObject_connect_1((XObject*)server,
                      XSignal(XMqttServer_clientConnected_signal),
                      (XObject*)server,
                      mock_on_client_connected,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)server,
                      XSignal(XMqttServer_clientDisconnected_signal),
                      (XObject*)server,
                      mock_on_client_disconnected,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)server,
                      XSignal(XMqttServer_messageReceived_signal),
                      (XObject*)server,
                      mock_on_message_received,
                      XConnectionType_Direct);
    MOCK_CHECK(mock_connect_existing(mock, T1, "sig-1", true, 0), "客户端 CONNECT 成功");
    MOCK_CHECK(g_sigConnectedCount == 1, "clientConnected 信号触发 1 次");
    mock_clear_output(mock, T1);
    packet = mock_publish("sig/hello", payload, sizeof(payload) - 1, 0, false, 0);
    MOCK_CHECK(packet != NULL, "构造 PUBLISH");
    if (packet) {
        XMqttServer_feedData(server, T1, (const uint8_t*)XByteArray_constData(packet),
                             (size_t)XByteArray_size_base(packet));
        XByteArray_delete_base(packet);
    }
    MOCK_CHECK(g_sigReceivedCount == 1, "messageReceived 信号触发 1 次");
    MOCK_CHECK(g_sigReceivedTransport == T1, "信号 transport 参数正确");
    MOCK_CHECK(strcmp(g_sigReceivedTopic, "sig/hello") == 0, "信号主题正确");
    MOCK_CHECK(g_sigReceivedPayloadSize == sizeof(payload) - 1 &&
               memcmp(g_sigReceivedPayload, payload, sizeof(payload) - 1) == 0,
               "信号载荷正确");
    XMqttServer_endClient(server, T1);
    MOCK_CHECK(g_sigDisconnectedCount >= 1, "clientDisconnected 信号触发");
    mock_server_delete(mock);
}

/* 21. 功能开关与服务器能力声明 */
static void mock_test_feature_switches(void)
{
    XMqttServerMock* mock;
    XMqttServer* server;
    XByteArray *body = NULL, *packet = NULL;
    const uint8_t* value;
    size_t valueLen;
    MockSubscribeItem item = {"sw/#", 1};
    XPrintf("---- 功能开关与服务器能力声明 ----\n");

    /* (1) 关闭保留消息：CONNACK 声明 + PUBLISH(retain) 被拒绝 */
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器（关闭保留消息）");
    if (!mock) return;
    server = (XMqttServer*)mock;
    XMqttServer_setRetainAvailable(server, false);
    MOCK_CHECK(mock_connect_v5_existing_ex(mock, T1, "sw-retain", 0, false),
               "v5 客户端 CONNECT 成功");
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x20, &body), "收到 CONNACK");
    if (body) {
        MOCK_CHECK(mock_connack_find_property(body, 0x25, &value, &valueLen) &&
                   valueLen >= 1 && value[0] == 0, "CONNACK 声明保留消息可用=0");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_clear_output(mock, T1);
    packet = mock_publish_v5("sw/x", NULL, (const uint8_t*)"r", 1, 0, true, 0);
    MOCK_CHECK(packet != NULL, "构造 v5 PUBLISH(retain=1)");
    if (packet) {
        XMqttServer_feedData(server, T1, (const uint8_t*)XByteArray_constData(packet),
                             (size_t)XByteArray_size_base(packet));
        XByteArray_delete_base(packet);
        packet = NULL;
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0xE0, &body), "收到 DISCONNECT");
    if (body) {
        MOCK_CHECK(XByteArray_size_base(body) >= 1 && XByteArray_constData(body)[0] == 0x9A,
                   "DISCONNECT 原因码 RetainNotSupported(0x9A)");
        XByteArray_delete_base(body);
        body = NULL;
    }
    MOCK_CHECK(mock->closeCount >= 1, "传输被服务器关闭");
    mock_server_delete(mock);

    /* (2) 关闭通配符订阅：CONNACK 声明 + SUBSCRIBE(#) 被拒绝 */
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器（关闭通配符）");
    if (!mock) return;
    server = (XMqttServer*)mock;
    XMqttServer_setWildcardAvailable(server, false);
    MOCK_CHECK(mock_connect_v5_existing_ex(mock, T1, "sw-wild", 0, false),
               "v5 客户端 CONNECT 成功");
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x20, &body), "收到 CONNACK");
    if (body) {
        MOCK_CHECK(mock_connack_find_property(body, 0x28, &value, &valueLen) &&
                   valueLen >= 1 && value[0] == 0, "CONNACK 声明通配符可用=0");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_clear_output(mock, T1);
    packet = mock_subscribe_v5(1, &item, 1);
    MOCK_CHECK(packet != NULL, "构造 v5 SUBSCRIBE sw/#");
    if (packet) {
        XMqttServer_feedData(server, T1, (const uint8_t*)XByteArray_constData(packet),
                             (size_t)XByteArray_size_base(packet));
        XByteArray_delete_base(packet);
        packet = NULL;
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x90, &body), "收到 SUBACK");
    if (body) {
        MOCK_CHECK(mock_suback_reason(body, true, 0) == 0xA2,
                   "SUBACK 原因码通配符不支持(0xA2)");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_server_delete(mock);

    /* (3) 关闭共享订阅：SUBSCRIBE($share) 被拒绝 */
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器（关闭共享订阅）");
    if (!mock) return;
    server = (XMqttServer*)mock;
    XMqttServer_setSharedAvailable(server, false);
    MOCK_CHECK(mock_connect_v5_existing_ex(mock, T1, "sw-share", 0, true),
               "v5 客户端 CONNECT 成功");
    {
        MockSubscribeItem shareItem = {"$share/grp/sw/#", 1};
        packet = mock_subscribe_v5(1, &shareItem, 1);
        MOCK_CHECK(packet != NULL, "构造 v5 SUBSCRIBE $share/grp/sw/#");
        if (packet) {
            XMqttServer_feedData(server, T1, (const uint8_t*)XByteArray_constData(packet),
                                 (size_t)XByteArray_size_base(packet));
            XByteArray_delete_base(packet);
            packet = NULL;
        }
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x90, &body), "收到 SUBACK");
    if (body) {
        MOCK_CHECK(mock_suback_reason(body, true, 0) == 0x9E,
                   "SUBACK 原因码共享订阅不支持(0x9E)");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_server_delete(mock);

    /* (4) 最大 QoS=1：CONNACK 声明 + PUBLISH(QoS2) 被拒绝 */
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器（最大 QoS 1）");
    if (!mock) return;
    server = (XMqttServer*)mock;
    XMqttServer_setMaximumQoS(server, 1);
    MOCK_CHECK(mock_connect_v5_existing_ex(mock, T1, "sw-qos", 0, false),
               "v5 客户端 CONNECT 成功");
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x20, &body), "收到 CONNACK");
    if (body) {
        MOCK_CHECK(mock_connack_find_property(body, 0x24, &value, &valueLen) &&
                   valueLen >= 1 && value[0] == 1, "CONNACK 声明最大 QoS=1");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_clear_output(mock, T1);
    packet = mock_publish_v5("sw/x", NULL, (const uint8_t*)"q2", 2, 2, false, 7);
    MOCK_CHECK(packet != NULL, "构造 v5 PUBLISH(QoS2)");
    if (packet) {
        XMqttServer_feedData(server, T1, (const uint8_t*)XByteArray_constData(packet),
                             (size_t)XByteArray_size_base(packet));
        XByteArray_delete_base(packet);
        packet = NULL;
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0xE0, &body), "收到 DISCONNECT");
    if (body) {
        MOCK_CHECK(XByteArray_size_base(body) >= 1 && XByteArray_constData(body)[0] == 0x9B,
                   "DISCONNECT 原因码 QoSNotSupported(0x9B)");
        XByteArray_delete_base(body);
        body = NULL;
    }
    MOCK_CHECK(mock->closeCount >= 1, "传输被服务器关闭");
    mock_server_delete(mock);

    /* (5) 最大 QoS=1：SUBSCRIBE(QoS2) 收紧为 QoSNotSupported */
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器（订阅 QoS 超限）");
    if (!mock) return;
    server = (XMqttServer*)mock;
    XMqttServer_setMaximumQoS(server, 1);
    MOCK_CHECK(mock_connect_v5_existing_ex(mock, T1, "sw-subqos", 0, true),
               "v5 客户端 CONNECT 成功");
    {
        MockSubscribeItem qos2Item = {"sw/qos2", 2};
        packet = mock_subscribe_v5(1, &qos2Item, 1);
        MOCK_CHECK(packet != NULL, "构造 v5 SUBSCRIBE(QoS2)");
        if (packet) {
            XMqttServer_feedData(server, T1, (const uint8_t*)XByteArray_constData(packet),
                                 (size_t)XByteArray_size_base(packet));
            XByteArray_delete_base(packet);
            packet = NULL;
        }
    }
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x90, &body), "收到 SUBACK");
    if (body) {
        MOCK_CHECK(mock_suback_reason(body, true, 0) == 0x9B,
                   "SUBACK 原因码 QoSNotSupported(0x9B)");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_server_delete(mock);

    /* (6) 最大报文大小：CONNACK 声明 + 超限报文被关闭 */
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器（限制报文大小）");
    if (!mock) return;
    server = (XMqttServer*)mock;
    XMqttServer_setMaximumPacketSize(server, 64);
    MOCK_CHECK(mock_connect_v5_existing_ex(mock, T1, "sw-size", 0, false),
               "v5 客户端 CONNECT 成功");
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x20, &body), "收到 CONNACK");
    if (body) {
        MOCK_CHECK(mock_connack_find_property(body, 0x27, &value, &valueLen) &&
                   valueLen >= 4 && value[0] == 0 && value[3] == 64,
                   "CONNACK 声明最大报文大小=64");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_clear_output(mock, T1);
    {
        uint8_t big[200];
        memset(big, 0xAB, sizeof(big));
        packet = mock_publish("sw/big", big, sizeof(big), 0, false, 0);
        MOCK_CHECK(packet != NULL, "构造超大 PUBLISH(200 字节)");
        if (packet) {
            XMqttServer_feedData(server, T1, (const uint8_t*)XByteArray_constData(packet),
                                 (size_t)XByteArray_size_base(packet));
            XByteArray_delete_base(packet);
            packet = NULL;
        }
    }
    MOCK_CHECK(mock->closeCount >= 1, "超限报文触发传输关闭");
    mock_server_delete(mock);

    /* (7) 关闭订阅标识符 + 服务器保活 + 主题别名上限：CONNACK 能力声明 */
    mock = mock_server_create();
    MOCK_CHECK(mock != NULL, "创建 Mock 服务器（能力声明）");
    if (!mock) return;
    server = (XMqttServer*)mock;
    XMqttServer_setSubscriptionIdAvailable(server, false);
    XMqttServer_setServerKeepAlive(server, 30);
    XMqttServer_setTopicAliasMaximum(server, 10);
    MOCK_CHECK(mock_connect_v5_existing_ex(mock, T1, "sw-cap", 0, false),
               "v5 客户端 CONNECT 成功");
    MOCK_CHECK(mock_find_packet(mock_output(mock, T1), 0x20, &body), "收到 CONNACK");
    if (body) {
        MOCK_CHECK(mock_connack_find_property(body, 0x29, &value, &valueLen) &&
                   valueLen >= 1 && value[0] == 0, "CONNACK 声明订阅标识符可用=0");
        MOCK_CHECK(mock_connack_find_property(body, 0x13, &value, &valueLen) &&
                   valueLen >= 2 && value[0] == 0 && value[1] == 30,
                   "CONNACK 声明服务器保活=30");
        MOCK_CHECK(mock_connack_find_property(body, 0x22, &value, &valueLen) &&
                   valueLen >= 2 && value[0] == 0 && value[1] == 10,
                   "CONNACK 声明主题别名上限=10");
        XByteArray_delete_base(body);
        body = NULL;
    }
    mock_server_delete(mock);
}

/* 22. 私有结构位域与固定头联合体 */
static void mock_test_data_layout(void)
{
    XPrintf("---- 私有结构位域与固定头联合体 ----\n");
    MOCK_CHECK(sizeof(XMqttFixedHeader) == 1, "XMqttFixedHeader 联合体占用 1 字节");
    {
        XMqttFixedHeader fh;
        fh.byte = 0x33; /* PUBLISH QoS1 + RETAIN */
        MOCK_CHECK(fh.bits.type == 3 && fh.bits.qos == 1 &&
                   fh.bits.retain == 1 && fh.bits.dup == 0,
                   "固定头联合体解析 0x33 正确");
        fh.bits.type = 3; fh.bits.qos = 2; fh.bits.dup = 1; fh.bits.retain = 0;
        MOCK_CHECK(fh.byte == 0x3C, "固定头联合体构造 PUBLISH QoS2+DUP 正确");
    }
    {
        XMqttServerClient c;
        memset(&c, 0, sizeof(c));
        c.protocolVersion = 5; c.cleanSession = 1; c.connected = 1;
        c.disconnectReceived = 1; c.willQoS = 2; c.willRetain = 1;
        MOCK_CHECK(c.protocolVersion == 5 && c.cleanSession &&
                   c.connected && c.disconnectReceived &&
                   c.willQoS == 2 && c.willRetain,
                   "XMqttServerClient 位域写入/读取一致");
        c.disconnectReceived = 0; c.willQoS = 0; c.willRetain = 0;
        MOCK_CHECK(!c.disconnectReceived && c.willQoS == 0 && !c.willRetain &&
                   c.protocolVersion == 5 && c.connected,
                   "XMqttServerClient 位域清零不影响相邻位");
    }
    {
        XMqttServerSubscription sub;
        memset(&sub, 0, sizeof(sub));
        sub.qos = 2; sub.noLocal = 1; sub.retainAsPublished = 1; sub.retainHandling = 2;
        MOCK_CHECK(sub.qos == 2 && sub.noLocal && sub.retainAsPublished &&
                   sub.retainHandling == 2, "XMqttServerSubscription 位域往返一致");
        XMqttServerQueuedMessage qm;
        memset(&qm, 0, sizeof(qm));
        qm.qos = 1; qm.retain = 1; qm.stage = 1;
        MOCK_CHECK(qm.qos == 1 && qm.retain && qm.stage == 1,
                   "XMqttServerQueuedMessage 位域往返一致");
        XMqttServerPendingWill pw;
        memset(&pw, 0, sizeof(pw));
        pw.qos = 2; pw.retain = 1;
        MOCK_CHECK(pw.qos == 2 && pw.retain, "XMqttServerPendingWill 位域往返一致");
        XMqttServerRetainedMessage rm;
        memset(&rm, 0, sizeof(rm));
        rm.qos = 2;
        MOCK_CHECK(rm.qos == 2, "XMqttServerRetainedMessage 位域往返一致");
        XMqttServerSession sn;
        memset(&sn, 0, sizeof(sn));
        sn.persistent = 1;
        MOCK_CHECK(sn.persistent, "XMqttServerSession 位域往返一致");
    }
}

/* ==================== 总入口 ==================== */

bool XMqttServerUnitTest_run(void)
{
    g_mockPass = 0;
    g_mockFail = 0;
    XPrintf("========== XMqttServer 单元测试开始 ==========\n");
    mock_test_connect();
    mock_test_publish_qos0();
    mock_test_publish_qos1();
    mock_test_publish_qos2();
    mock_test_retained();
    mock_test_unsubscribe();
    mock_test_ping();
    mock_test_will();
    mock_test_disconnect_clean();
    mock_test_auth();
    mock_test_server_publish();
    mock_test_persistent_session();
    mock_test_shared_subscription();
    mock_test_topic_alias();
    mock_test_clientid_takeover();
    mock_test_config_api();
    mock_test_null_safety();
    mock_test_base_virtuals();
    mock_test_publish_with_properties();
    mock_test_signals();
    mock_test_feature_switches();
    mock_test_data_layout();
    XPrintf("========== XMqttServer 单元测试完成: %d 通过, %d 失败 ==========\n",
            g_mockPass, g_mockFail);
    return g_mockFail == 0;
}
