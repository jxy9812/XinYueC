#include "XModbus_config.h"
#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_CLIENT_ON
#include "XModbusClient.h"
#include "XModbusClient_Protected.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"
#include "XByteArray.h"
#include "XHashMap.h"
#include <string.h>

// 虚函数重载声明
static void VXModbusClient_deinit(XModbusClient* client);
static bool VXModbusClient_processResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);
static bool VXModbusClient_processPrivateResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);
static XModbusReply* VXModbusClient_sendRawRequest(XModbusClient* client, const XModbusRequest* request, int serverAddress, XFuncParamType type);

// =============== 字节序辅助函数 ===============
// 从大端序数据读取uint16
static inline uint16_t readUint16BE(const uint8_t* data, size_t offset)
{
    uint16_t value;
    XMemory_read_data(data + offset, XBYTE_ORDER_BIG_ENDIAN, (uint8_t*)&value, sizeof(uint16_t));
    return value;
}

// 写入uint16到大端序数据
static inline void writeUint16BE(uint8_t* data, size_t offset, uint16_t value)
{
    XMemory_write_data(data + offset, XBYTE_ORDER_BIG_ENDIAN, (const uint8_t*)&value, sizeof(uint16_t));
}

// ================== 虚表初始化 ==================
XVtable* XModbusClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusClient)
        // 继承 XModbusDevice
        XVTABLE_INHERIT_XCLASS(XModbusDevice);

    void* table[] = {
        VXModbusClient_processResponse,
        VXModbusClient_processPrivateResponse,
        VXModbusClient_sendRawRequest
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

    // 重载析构
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusClient_deinit);

    XCLASS_SHOW_SIZE_DEFAULT(XModbusClient);
    return XVTABLE_DEFAULT;
}

// ================== 构造/析构 ==================
XModbusClient* XModbusClient_create_ex(XMemoryType memory) {
    XModbusClient* client = (XModbusClient*)XMemory_malloc(sizeof(XModbusClient), memory);
    if (client) {
        XModbusClient_init(client);
        Set_Class_Memory(client, memory); Set_Class_IsHeap(client, true);
    }
    return client;
}

void XModbusClient_init(XModbusClient* client) {
    if (!client) return;

    // 初始化基类
    XModbusDevice_init((XModbusDevice*)client);
    XClassGetVtable(client) = XModbusClient_class_init();

    // 初始化成员变量
    client->m_timeout = 1000; // 对齐 Qt QModbusClient 默认 1000 毫秒
    client->m_numberOfRetries = 3; // 默认3次
    client->m_timeoutTimer= XTIMER_INVALID_ID;
    client->m_poolMap = NULL;

    // 初始化自动重连配置
    client->m_autoReconnect = false;       // 默认关闭
    client->m_reconnectInterval = 1000;    // 默认1秒
    client->m_maxReconnectAttempts = -1;   // 默认无限次
    client->m_reconnectAttempts = 0;       // 当前重连次数
    client->m_reconnectTimer = XTIMER_INVALID_ID;
}

static void VXModbusClient_deinit(XModbusClient* client) {
    if (!client) return;

    // 停止重连定时器
    XModbusClient_timeoutTimerStop(client);
    if (client->m_reconnectTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)client, client->m_reconnectTimer);
        client->m_reconnectTimer = XTIMER_INVALID_ID;
    }
    if (client->m_poolMap)
    {
        XMapBase_delete_base(client->m_poolMap);
        client->m_poolMap = NULL;
    }
    // 调用基类析构
    XClass_Deinit_Parent(XModbusDevice, client);
}

// ================== 辅助函数：根据XModbusDataUnit构建请求PDU ==================
static XModbusRequest* buildReadRequest(const XModbusDataUnit* unit) {
    if (!unit || !XModbusDataUnit_isValid(unit)) return NULL;

    XModbusPdu_FunctionCode code;
    switch (unit->m_type) {
    case XModbusCoils: code = XModbusPdu_ReadCoils; break;
    case XModbusDiscreteInputs: code = XModbusPdu_ReadDiscreteInputs; break;
    case XModbusInputRegisters: code = XModbusPdu_ReadInputRegisters; break;
    case XModbusHoldingRegisters: code = XModbusPdu_ReadHoldingRegisters; break;
    default: return NULL;
    }

    uint16_t startAddress = (uint16_t)XModbusDataUnit_startAddress(unit);
    uint16_t quantity = (uint16_t)XModbusDataUnit_valueCount(unit);

    // 检查数量是否在有效范围内
    if (quantity == 0 || quantity > 2000) return NULL;

    uint8_t data[4];
    writeUint16BE(data, 0, startAddress);
    writeUint16BE(data, 2, quantity);

    XModbusRequest* req = XModbusRequest_create();
    if (!req) return NULL;
    XModbusPdu_setFunctionCode(req, code);
    XModbusPdu_setData(req, data, 4);
    return req;
}

static XModbusRequest* buildWriteRequest(const XModbusDataUnit* unit) {
    if (!unit || !XModbusDataUnit_isValid(unit)) return NULL;

    size_t count = XModbusDataUnit_valueCount(unit);
    if (count == 0) return NULL;

    XModbusPdu_FunctionCode code;
    XModbusRequest* req = XModbusRequest_create();
    if (!req) return NULL;
    XByteArray* payload = ((XModbusPdu*)req)->m_data;

    if (count == 1) {
        // Single Write
        if (unit->m_type == XModbusCoils) {
            code = XModbusPdu_WriteSingleCoil;
        }
        else if (unit->m_type == XModbusHoldingRegisters) {
            code = XModbusPdu_WriteSingleRegister;
        }
        else {
            return NULL;
        }

        uint16_t address = (uint16_t)XModbusDataUnit_startAddress(unit);
        uint16_t value = (uint16_t)XModbusDataUnit_value(unit, 0);
        if (unit->m_type == XModbusCoils) {
            value = value ? 0xFF00 : 0x0000; // Coils use 0xFF00 for ON
        }

        uint8_t data[4];
        writeUint16BE(data, 0, address);
        writeUint16BE(data, 2, value);
        XByteArray_push_back_2(payload, data, 4);
    }
    else {
        // Multiple Write
        if (unit->m_type == XModbusCoils) {
            code = XModbusPdu_WriteMultipleCoils;
        }
        else if (unit->m_type == XModbusHoldingRegisters) {
            code = XModbusPdu_WriteMultipleRegisters;
        }
        else {
            return NULL;
        }

        uint16_t address = (uint16_t)XModbusDataUnit_startAddress(unit);
        uint16_t byteCount = (count % 8 == 0) ? (count / 8) : (count / 8 + 1);

        uint8_t header[5];
        writeUint16BE(header, 0, address);
        writeUint16BE(header, 2, count);
        header[4] = byteCount;
        XByteArray_push_back_2(payload, header, 5);

        // Pack coils into bytes
        if (unit->m_type == XModbusCoils) {
            uint8_t coilByte = 0;
            for (size_t i = 0; i < count; i++) {
                if (XModbusDataUnit_value(unit, i)) {
                    coilByte |= (1 << (i % 8));
                }
                if ((i % 8) == 7 || i == count - 1) {
                    XByteArray_push_back_1(payload, coilByte);
                    coilByte = 0;
                }
            }
        }
        else {
            // Holding Registers - 大端序
            for (size_t i = 0; i < count; i++) {
                uint16_t val = (uint16_t)XModbusDataUnit_value(unit, i);
                uint8_t valBytes[2];
                XMemory_write_data(valBytes, XBYTE_ORDER_BIG_ENDIAN, (const uint8_t*)&val, 2);
                XByteArray_push_back_2(payload, valBytes, 2);
            }
        }
    }
    XModbusPdu_setFunctionCode(req, code);
    return req;
}

static XModbusRequest* buildReadWriteRequest(const XModbusDataUnit* readUnit, const XModbusDataUnit* writeUnit) {
    if (!readUnit || !writeUnit ||
        !XModbusDataUnit_isValid(readUnit) || !XModbusDataUnit_isValid(writeUnit)) {
        return NULL;
    }
    XModbusRequest* req = XModbusRequest_create();
    if (!req) return NULL;

    // 只支持保持寄存器
    if (readUnit->m_type != XModbusHoldingRegisters || writeUnit->m_type != XModbusHoldingRegisters) {
        return NULL;
    }

    size_t readCount = XModbusDataUnit_valueCount(readUnit);
    size_t writeCount = XModbusDataUnit_valueCount(writeUnit);

    if (readCount == 0 || readCount > 125 || writeCount == 0 || writeCount > 121) {
        return NULL;  // Modbus规范限制
    }

    uint16_t readStartAddr = (uint16_t)XModbusDataUnit_startAddress(readUnit);
    uint16_t writeStartAddr = (uint16_t)XModbusDataUnit_startAddress(writeUnit);
    uint8_t writeByteCount = (uint8_t)(writeCount * 2);

    size_t dataSize = 9 + writeByteCount;
    XByteArray* pdu = ((XModbusPdu*)req)->m_data;
    XByteArray_resize_base(pdu, dataSize);
    uint8_t* data = XContainerDataAddr(pdu);

    // 大端序写入各字段
    writeUint16BE(data, 0, readStartAddr);
    writeUint16BE(data, 2, readCount);
    writeUint16BE(data, 4, writeStartAddr);
    writeUint16BE(data, 6, writeCount);
    data[8] = writeByteCount;

    // 写入数据（大端序）
    for (size_t i = 0; i < writeCount; i++) {
        uint16_t val = (uint16_t)XModbusDataUnit_value(writeUnit, i);
        writeUint16BE(data, 9 + i * 2, val);
    }

    XModbusPdu_setFunctionCode(req, XModbusPdu_ReadWriteMultipleRegisters);
    return req;
}

// ================== Public API ==================
XModbusReply* XModbusClient_sendReadRequest(XModbusClient* client, const XModbusDataUnit* read, int serverAddress) {
    if (!client || !read || serverAddress < 0) {
        return NULL;
    }
    XModbusRequest* request = buildReadRequest(read);
    if (!request) {
        return NULL;
    }
    XModbusReply* reply = XModbusClient_sendRawRequest_ref_base(client, request, serverAddress);
    //XModbusRequest_delete_base(request);
    return reply;
}

XModbusReply* XModbusClient_pollReadRequest(XModbusClient* client, const XModbusDataUnit* read, int serverAddress, int pollIntervalMs)
{
    if (!client || !read || serverAddress < 0) {
        return NULL;
    }
    XModbusRequest* request = buildReadRequest(read);
    if (!request) {
        return NULL;
    }
    XModbusReply* reply = XModbusClient_pollRawRequest_ref(client, request, serverAddress, pollIntervalMs);
    //XModbusRequest_delete_base(request);
    return reply;
}

XModbusReply* XModbusClient_sendWriteRequest(XModbusClient* client, const XModbusDataUnit* write, int serverAddress) {
    if (!client || !write || serverAddress < 0) {
        return NULL;
    }
    XModbusRequest* request = buildWriteRequest(write);
    if (!request) {
        return NULL;
    }
    XModbusReply* reply = XModbusClient_sendRawRequest_ref_base(client, request, serverAddress);
    //XModbusRequest_delete_base(request);
    return reply;
}

XModbusReply* XModbusClient_pollWriteRequest(XModbusClient* client, const XModbusDataUnit* write, int serverAddress, int pollIntervalMs)
{
    if (!client || !write || serverAddress < 0) {
        return NULL;
    }
    XModbusRequest* request = buildWriteRequest(write);
    if (!request) {
        return NULL;
    }
    XModbusReply* reply = XModbusClient_pollRawRequest_ref(client, request, serverAddress, pollIntervalMs);
    //XModbusRequest_delete_base(request);
    return reply;
}

XModbusReply* XModbusClient_sendReadWriteRequest(XModbusClient* client, const XModbusDataUnit* read, const XModbusDataUnit* write, int serverAddress) {
    if (!client || !read || !write || serverAddress < 0) {
        return NULL;
    }

    XModbusRequest* request = buildReadWriteRequest(read, write);
    if (!request) {
        return NULL;
    }

    XModbusReply* reply = XModbusClient_sendRawRequest_ref_base(client, request, serverAddress);
    //XModbusRequest_delete_base(request);
    return reply;
}

XModbusReply* XModbusClient_pollReadWriteRequest(XModbusClient* client, const XModbusDataUnit* read, const XModbusDataUnit* write, int serverAddress, int pollIntervalMs)
{
    if (!client || !read || !write || serverAddress < 0) {
        return NULL;
    }

    XModbusRequest* request = buildReadWriteRequest(read, write);
    if (!request) {
        return NULL;
    }

    XModbusReply* reply = XModbusClient_pollRawRequest_ref(client, request, serverAddress, pollIntervalMs);
    //XModbusRequest_delete_base(request);
    return reply;
}

// ================== sendRawRequest 虚函数实现 ==================

/**
 * @brief 默认的 sendRawRequest 实现
 * @note 基类不提供实际发送功能，子类必须重写此函数
 */
static XModbusReply* VXModbusClient_sendRawRequest(XModbusClient* client, const XModbusRequest* request, int serverAddress,XFuncParamType type) {
    // 基类是抽象的，不提供默认实现
    (void)client; (void)request; (void)serverAddress;
    return NULL;
}

/**
 * @brief 通过虚函数表调用 sendRawRequest
 */
XModbusReply* XModbusClient_sendRawRequest_base(XModbusClient* client, const XModbusRequest* request, int serverAddress) {
    if (!client || !XClassGetVtable(client)) return NULL;
    return  XClassGetVirtualFunc(client, EXModbusClient_SendRawRequest,XModbusReply * (*)(XModbusClient*, const XModbusRequest*, int, XFuncParamType))(client, request, serverAddress, XFuncParamType_Copy);
}

XModbusReply* XModbusClient_sendRawRequest_move_base(XModbusClient* client, const XModbusRequest* request, int serverAddress)
{
    if (!client || !XClassGetVtable(client)) return NULL;
    return  XClassGetVirtualFunc(client, EXModbusClient_SendRawRequest, XModbusReply * (*)(XModbusClient*, const XModbusRequest*, int, XFuncParamType))(client, request, serverAddress, XFuncParamType_Move);
}

XModbusReply* XModbusClient_sendRawRequest_ref_base(XModbusClient* client, const XModbusRequest* request, int serverAddress)
{
    if (!client || !XClassGetVtable(client)) return NULL;
    return  XClassGetVirtualFunc(client, EXModbusClient_SendRawRequest, XModbusReply * (*)(XModbusClient*, const XModbusRequest*, int, XFuncParamType))(client, request, serverAddress, XFuncParamType_Ref);
}

XModbusReply* XModbusClient_pollRawRequest(XModbusClient* client, const XModbusRequest* request, int serverAddress, int pollIntervalMs)
{
    if (!client || !request || serverAddress < 0) {
        return NULL;
    }
    XModbusRequest* req = XModbusRequest_create_copy(request);
    if (!req)return NULL;
    XModbusReply* reply = XModbusClient_pollRawRequest_ref(client, req, serverAddress, pollIntervalMs);
    if (!reply)XModbusRequest_delete_base(req);
    return reply;
}

XModbusReply* XModbusClient_pollRawRequest_move(XModbusClient* client, const XModbusRequest* request, int serverAddress, int pollIntervalMs)
{
    if (!client || !request || serverAddress < 0) {
        return NULL;
    }
    XModbusRequest* req=XModbusRequest_create_move(request);
    if (!req)return NULL;
    XModbusReply* reply=XModbusClient_pollRawRequest_ref(client, req, serverAddress, pollIntervalMs);
    if (!reply)XModbusRequest_delete_base(req);
    return reply;
}

XModbusReply* XModbusClient_pollRawRequest_ref(XModbusClient* client, XModbusRequest* request, int serverAddress, int pollIntervalMs)
{
    if (!client || !request || serverAddress < 0) {
        return NULL;
    }
    XModbusReply* reply = XModbusClient_createReply_ref(client, request, serverAddress);
    if (!reply) {
        return NULL;
    }
    XTimerId id = XObject_startTimer_ms(client, pollIntervalMs, XTimerType_CoarseTimer);
    if (id == XTIMER_INVALID_ID)
    {
        XModbusReply_deleteLater(reply);
        return NULL;
    }
    if (!client->m_poolMap)
    {
        client->m_poolMap = XHashMap_Create(XTimerId, XModbusReply*, size_t_compare);
    }
    XHashMap_insert_base(client->m_poolMap, &id, &reply);
    return reply;
}

/**
 * @brief 辅助函数：创建并初始化 Reply 对象
 */
XModbusReply* XModbusClient_createReply(XModbusClient* client, const XModbusRequest* request, int serverAddress) {
    if (!client || !request || serverAddress < 0) {
        return NULL;
    }
    XModbusRequest* req = XModbusRequest_create_copy(request);
    if (!req) return NULL;
    XModbusReply* reply = XModbusClient_createReply_ref(client, req, serverAddress);
    if (!reply)
        XModbusRequest_delete_base(req);
    return reply;
}

XModbusReply* XModbusClient_createReply_move(XModbusClient* client, XModbusRequest* request, int serverAddress)
{
    if (!client || !request || serverAddress < 0) {
        return NULL;
    }
    XModbusRequest* req = XModbusRequest_create_move(request);
    if (!req)return NULL;
    XModbusReply* reply = XModbusClient_createReply_ref(client, req, serverAddress);
    if (!reply)
        XModbusRequest_delete_base(req);
    return reply;
}

XModbusReply* XModbusClient_createReply_ref(XModbusClient* client, XModbusRequest* request, int serverAddress)
{
    if (!client || !request || serverAddress < 0) {
        return NULL;
    }

    // 确定回复类型
    XModbusReply_ReplyType type = XModbusReply_Raw;
    XModbusPdu_FunctionCode fc = XModbusPdu_functionCode((const XModbusPdu*)request);

    if (fc == XModbusPdu_ReadCoils ||
        fc == XModbusPdu_ReadDiscreteInputs ||
        fc == XModbusPdu_ReadInputRegisters ||
        fc == XModbusPdu_ReadHoldingRegisters ||
        fc == XModbusPdu_WriteSingleCoil ||
        fc == XModbusPdu_WriteSingleRegister ||
        fc == XModbusPdu_WriteMultipleCoils ||
        fc == XModbusPdu_WriteMultipleRegisters ||
        fc == XModbusPdu_ReadWriteMultipleRegisters) {
        type = XModbusReply_Common;
    }

    // 创建 Reply 对象
    XModbusReply* reply = XModbusReply_create(type, serverAddress);
    if (!reply) return NULL;
    reply->m_request = request;
    return reply;
}

bool XModbusClient_cancelPoll(XModbusClient* client, XModbusReply* reply)
{
    if (!client || !reply ||!client->m_poolMap) {
        return false;
    }
    for_each_iterator(client->m_poolMap,XHashMap,it)
    {
        XPair* pair = XHashMap_iterator_data(&it);
        if (reply == XPair_Second(pair, XModbusReply*))
        {
            XObject_killTimer(client, XPair_First(pair, XTimerId));
            XHashMap_erase_base(client->m_poolMap, &it, NULL);
            return true;
        }
    }
    return false;
}

// --- Configuration Getters/Setters ---
size_t XModbusClient_timeout(const XModbusClient* client) {
    return client ? client->m_timeout : 1000;
}

void XModbusClient_setTimeout(XModbusClient* client, size_t newTimeout) {
    if (!client || client->m_timeout == newTimeout) return;
    client->m_timeout = newTimeout;
    XModbusClient_timeoutChanged_signal(client, newTimeout);
}

int16_t XModbusClient_numberOfRetries(const XModbusClient* client) {
    return client ? client->m_numberOfRetries : 3;
}

void XModbusClient_setNumberOfRetries(XModbusClient* client, uint8_t number) {
    if (!client) return;
    client->m_numberOfRetries = number;
}
// ================== 自动重连配置 API ==================
bool XModbusClient_autoReconnect(const XModbusClient* client) {
    return client ? client->m_autoReconnect : false;
}
void XModbusClient_setAutoReconnect(XModbusClient* client, bool enabled) {
    if (!client) return;
    client->m_autoReconnect = enabled;

    // 如果禁用自动重连，停止正在进行的重连定时器
    if (!enabled && client->m_reconnectTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)client, client->m_reconnectTimer);
        client->m_reconnectTimer = XTIMER_INVALID_ID;
    }
}
size_t XModbusClient_reconnectInterval(const XModbusClient* client) {
    return client ? client->m_reconnectInterval : 1000;
}
void XModbusClient_setReconnectInterval(XModbusClient* client, size_t interval) {
    if (!client) return;
    client->m_reconnectInterval = interval;
}
int16_t XModbusClient_maxReconnectAttempts(const XModbusClient* client) {
    return client ? client->m_maxReconnectAttempts : -1;
}
void XModbusClient_setMaxReconnectAttempts(XModbusClient* client, int16_t attempts) {
    if (!client) return;
    client->m_maxReconnectAttempts = attempts;
}
int16_t XModbusClient_reconnectAttempts(const XModbusClient* client) {
    return client ? client->m_reconnectAttempts : 0;
}

// ================== 受保护的API实现（供子类调用）==================

bool XModbusClient_processResponse_base(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data)
{
    if (!client || !XClassGetVtable(client))
        return false;
    return XClassGetVirtualFunc(client, EXModbusClient_ProcessResponse,
        bool(*)(XModbusClient*, const XModbusResponse*, XModbusDataUnit*))(client, response, data);
}

bool XModbusClient_processPrivateResponse_base(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data)
{
    if (!client || !XClassGetVtable(client))
        return false;
    return XClassGetVirtualFunc(client, EXModbusClient_ProcessPrivateResponse,
        bool(*)(XModbusClient*, const XModbusResponse*, XModbusDataUnit*))(client, response, data);
}

// ================== 虚函数默认实现 ==================

 /**
  * @brief 默认 ProcessResponse 实现 - 处理标准Modbus响应
  */
static bool VXModbusClient_processResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data)
{
    (void)client;
    if (!response || !data) return false;

    XModbusPdu_FunctionCode fc = XModbusPdu_functionCode(&response->m_base);
    bool isException = XModbusPdu_isException(&response->m_base);

    // 处理异常响应
    if (isException) {
        return false;
    }

    const uint8_t* respData = XContainerDataAddr(((XModbusPdu*)response)->m_data);
    size_t respSize = XByteArray_size_base(((XModbusPdu*)response)->m_data);

    // =============== 读线圈响应 (FC 01) ===============
    if (fc == XModbusPdu_ReadCoils) {
        if (respSize < 1) return false;

        uint8_t byteCount = respData[0];
        if (byteCount == 0 || (size_t)(byteCount + 1) > respSize) return false;

        data->m_type = XModbusCoils;

        size_t bitCount = byteCount * 8;
        XModbusDataUnit_setValueCount(data, bitCount);

        size_t count = XModbusDataUnit_valueCount(data);
        size_t bitIndex = 0;
        for (int i = 1; i <= byteCount && bitIndex < count; i++) {
            uint8_t byteVal = respData[i];
            for (int j = 0; j < 8 && bitIndex < count; j++) {
                XModbusDataUnit_setValue(data, bitIndex, (byteVal >> j) & 1);
                bitIndex++;
            }
        }
        return true;
    }

    // =============== 读离散输入响应 (FC 02) ===============
    if (fc == XModbusPdu_ReadDiscreteInputs) {
        if (respSize < 1) return false;

        uint8_t byteCount = respData[0];
        if (byteCount == 0 || (size_t)(byteCount + 1) > respSize) return false;

        data->m_type = XModbusDiscreteInputs;

        size_t bitCount = byteCount * 8;
        XModbusDataUnit_setValueCount(data, bitCount);

        size_t count = XModbusDataUnit_valueCount(data);
        size_t bitIndex = 0;
        for (int i = 1; i <= byteCount && bitIndex < count; i++) {
            uint8_t byteVal = respData[i];
            for (int j = 0; j < 8 && bitIndex < count; j++) {
                XModbusDataUnit_setValue(data, bitIndex, (byteVal >> j) & 1);
                bitIndex++;
            }
        }
        return true;
    }

    // =============== 读保持寄存器响应 (FC 03) ===============
    if (fc == XModbusPdu_ReadHoldingRegisters) {
        if (respSize < 1) return false;

        uint8_t byteCount = respData[0];
        if (byteCount % 2 != 0 || (size_t)(byteCount + 1) > respSize) return false;

        data->m_type = XModbusHoldingRegisters;

        size_t regCount = byteCount / 2;
        XModbusDataUnit_setValueCount(data, regCount);

        for (size_t i = 0; i < regCount; i++) {
            uint16_t reg = readUint16BE(respData, 1 + i * 2);
            XModbusDataUnit_setValue(data, i, reg);
        }
        return true;
    }

    // =============== 读输入寄存器响应 (FC 04) ===============
    if (fc == XModbusPdu_ReadInputRegisters) {
        if (respSize < 1) return false;

        uint8_t byteCount = respData[0];
        if (byteCount % 2 != 0 || (size_t)(byteCount + 1) > respSize) return false;

        data->m_type = XModbusInputRegisters;

        size_t regCount = byteCount / 2;
        XModbusDataUnit_setValueCount(data, regCount);

        for (size_t i = 0; i < regCount; i++) {
            uint16_t reg = readUint16BE(respData, 1 + i * 2);
            XModbusDataUnit_setValue(data, i, reg);
        }
        return true;
    }

    // =============== 写单个线圈响应 (FC 05) ===============
    if (fc == XModbusPdu_WriteSingleCoil) {
        // 标准响应：Address(2) + Value(2)，共 4 字节
        // 简化响应：Address(2) + Value(1)，共 3 字节
        if (respSize < 3) return false;

        uint16_t address = readUint16BE(respData, 0);
        uint16_t value;

        if (respSize >= 4) {
            // 标准响应
            value = readUint16BE(respData, 2);
        }
        else {
            // 简化响应：1 字节 Value
            value = (respData[2] == 0xFF) ? 0xFF00 : 0x0000;
        }

        data->m_type = XModbusCoils;
        XModbusDataUnit_setStartAddress(data, address);
        XModbusDataUnit_setValueCount(data, 1);
        XModbusDataUnit_setValue(data, 0, (value == 0xFF00) ? 1 : 0);
        return true;
    }

    // =============== 写单个寄存器响应 (FC 06) ===============
    if (fc == XModbusPdu_WriteSingleRegister) {
        if (respSize < 4) return false;

        uint16_t address = readUint16BE(respData, 0);
        uint16_t value = readUint16BE(respData, 2);

        data->m_type = XModbusHoldingRegisters;
        XModbusDataUnit_setStartAddress(data, address);
        XModbusDataUnit_setValueCount(data, 1);
        XModbusDataUnit_setValue(data, 0, value);
        return true;
    }

    // =============== 写多个线圈响应 (FC 15) ===============
    if (fc == XModbusPdu_WriteMultipleCoils) {
        if (respSize < 4) return false;

        uint16_t address = readUint16BE(respData, 0);
        uint16_t quantity = readUint16BE(respData, 2);

        data->m_type = XModbusCoils;
        XModbusDataUnit_setStartAddress(data, address);
        XModbusDataUnit_setValueCount(data, quantity);
        return true;
    }

    // =============== 写多个寄存器响应 (FC 16) ===============
    if (fc == XModbusPdu_WriteMultipleRegisters) {
        if (respSize < 4) return false;

        uint16_t address = readUint16BE(respData, 0);
        uint16_t quantity = readUint16BE(respData, 2);

        data->m_type = XModbusHoldingRegisters;
        XModbusDataUnit_setStartAddress(data, address);
        XModbusDataUnit_setValueCount(data, quantity);
        return true;
    }

    // =============== 读写多个寄存器响应 (FC 23) ===============
    if (fc == XModbusPdu_ReadWriteMultipleRegisters) {
        if (respSize < 1) return false;

        uint8_t byteCount = respData[0];
        if (byteCount % 2 != 0 || (size_t)(byteCount + 1) > respSize) return false;

        data->m_type = XModbusHoldingRegisters;

        size_t regCount = byteCount / 2;
        XModbusDataUnit_setValueCount(data, regCount);

        for (size_t i = 0; i < regCount; i++) {
            uint16_t reg = readUint16BE(respData, 1 + i * 2);
            XModbusDataUnit_setValue(data, i, reg);
        }
        return true;
    }

    // =============== 非标准功能码，委托给私有响应处理 ===============
    return XModbusClient_processPrivateResponse_base(client, response, data);
}

/**
 * @brief 默认 ProcessPrivateResponse 实现 - 处理自定义/私有功能码
 */
static bool VXModbusClient_processPrivateResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data)
{
    (void)client; (void)response; (void)data;
    // 基类不处理私有响应
    return false;
}

// ================== 信号 ==================
void* XModbusClient_timeoutChanged_signal(XModbusClient* client, int newTimeout) {
    XEmitSignal(client, XModbusClient_timeoutChanged_signal,
        XVarList_Create(XVar(int, newTimeout)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void XModbusClient_timeoutTimerStop(XModbusClient* client)
{
    if (!client)return;
    if (client->m_timeoutTimer != XTIMER_INVALID_ID) 
    {
        XObject_killTimer((XObject*)client, client->m_timeoutTimer);
        client->m_timeoutTimer = XTIMER_INVALID_ID;
    }
}

void XModbusClient_timeoutTimerStart(XModbusClient* client)
{
    if (!client)return;
    XModbusClient_timeoutTimerStop(client);
    client->m_timeoutTimer = XObject_startTimer_ms((XObject*)client,
        XModbusClient_timeout(client), XTimerType_CoarseTimer);
}

void XModbusClient_reconnectTimerStop(XModbusClient* client)
{
    if (!client)return;
    if (client->m_reconnectTimer != XTIMER_INVALID_ID)
    {
        XObject_killTimer((XObject*)client, client->m_reconnectTimer);
        client->m_reconnectTimer = XTIMER_INVALID_ID;
    }
}

void XModbusClient_reconnectTimerStart(XModbusClient* client)
{
    if (!client)return;
    XModbusClient_reconnectTimerStop(client);
    client->m_reconnectTimer = XObject_startTimer_ms((XObject*)client,
        XModbusClient_reconnectInterval(client), XTimerType_CoarseTimer);
    //XPrintf("reconnectTimer:%d\n", client->m_reconnectTimer);
}

#endif /* XMODBUS_CLIENT_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */
