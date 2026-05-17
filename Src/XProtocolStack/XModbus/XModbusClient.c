#include "XModbusClient.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"
#include "XByteArray.h"
#include <string.h>


// 虚函数重载声明
static void VXModbusClient_deinit(XModbusClient* client);
static bool VXModbusClient_processResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);
static bool VXModbusClient_processPrivateResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);
// ================== 虚表初始化 ==================
XVtable* XModbusClient_class_init(void) 
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XModbusDevice))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
     // 继承 XModbusDevice
     XVTABLE_INHERIT_XCLASS(XModbusDevice);
    void* table[] = { VXModbusClient_processResponse,VXModbusClient_processPrivateResponse };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
     // 重载析构
     XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusClient_deinit);
#if SHOWCONTAINERSIZE
    printf("XModbusClient size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// ================== 构造/析构 ==================
XModbusClient* XModbusClient_create(void) {
    XModbusClient* client = (XModbusClient*)XMalloc_System(sizeof(XModbusClient));
    if (client) {
        XModbusClient_init(client);
        Set_Class_MemoryFree(client, XFree_System);
    }
    return client;
}

void XModbusClient_init(XModbusClient* client) {
    if (!client) return;

    // 初始化基类
    XModbusDevice_init((XModbusDevice*)client);
    XClassGetVtable(client) = XModbusClient_class_init();

    // 初始化成员变量
    client->m_timeout = 1000; // 默认1秒
    client->m_numberOfRetries = 3; // 默认3次
}

static void VXModbusClient_deinit(XModbusClient* client) {
    if (!client) return;
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
    if (quantity == 0 || quantity > 2000) return NULL; // Approximate max for most functions

    uint8_t data[4] = {
        (uint8_t)((startAddress >> 8) & 0xFF),
        (uint8_t)(startAddress & 0xFF),
        (uint8_t)((quantity >> 8) & 0xFF),
        (uint8_t)(quantity & 0xFF)
    };

    return XModbusRequest_create_with_code_and_data(code, data, 4);
}

static XModbusRequest* buildWriteRequest(const XModbusDataUnit* unit) {
    if (!unit || !XModbusDataUnit_isValid(unit)) return NULL;

    size_t count = XModbusDataUnit_valueCount(unit);
    if (count == 0) return NULL;

    XModbusPdu_FunctionCode code;
    XByteArray* payload = XByteArray_create();

    if (count == 1) {
        // Single Write
        if (unit->m_type == XModbusCoils) {
            code = XModbusPdu_WriteSingleCoil;
        }
        else if (unit->m_type == XModbusHoldingRegisters) {
            code = XModbusPdu_WriteSingleRegister;
        }
        else {
            XByteArray_delete_base(payload);
            return NULL;
        }

        uint16_t address = (uint16_t)XModbusDataUnit_startAddress(unit);
        uint16_t value = (uint16_t)XModbusDataUnit_value(unit, 0);
        if (unit->m_type == XModbusCoils) {
            value = value ? 0xFF00 : 0x0000; // Coils use 0xFF00 for ON
        }

        uint8_t data[4] = {
            (uint8_t)((address >> 8) & 0xFF),
            (uint8_t)(address & 0xFF),
            (uint8_t)((value >> 8) & 0xFF),
            (uint8_t)(value & 0xFF)
        };
        XByteArray_append_array_base(payload, data, 4);
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
            XByteArray_delete_base(payload);
            return NULL;
        }

        uint16_t address = (uint16_t)XModbusDataUnit_startAddress(unit);
        uint16_t byteCount = (count % 8 == 0) ? (count / 8) : (count / 8 + 1);

        uint8_t header[5] = {
            (uint8_t)((address >> 8) & 0xFF),
            (uint8_t)(address & 0xFF),
            (uint8_t)((count >> 8) & 0xFF),
            (uint8_t)(count & 0xFF),
            byteCount
        };
        XByteArray_append_array_base(payload, header, 5);

        // Pack coils into bytes
        if (unit->m_type == XModbusCoils) {
            uint8_t coilByte = 0;
            for (size_t i = 0; i < count; i++) {
                if (XModbusDataUnit_value(unit, i)) {
                    coilByte |= (1 << (i % 8));
                }
                if ((i % 8) == 7 || i == count - 1) {
                    XByteArray_push_back_base(payload, coilByte);
                    coilByte = 0;
                }
            }
        }
        else {
            // Holding Registers
            for (size_t i = 0; i < count; i++) {
                uint16_t val = (uint16_t)XModbusDataUnit_value(unit, i);
                XByteArray_push_back_base(payload, (uint8_t)((val >> 8) & 0xFF));
                XByteArray_push_back_base(payload, (uint8_t)(val & 0xFF));
            }
        }
    }

    XModbusRequest* req = XModbusRequest_create_with_code(code);
    if (req) {
        XModbusPdu_setData(&req->m_base, XContainerSharedDataPtr(payload), XByteArray_size_base(payload));
    }
    XByteArray_delete_base(payload);
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
    XModbusReply* reply = XModbusClient_sendRawRequest(client, request, serverAddress);
    XModbusRequest_delete_base(request);
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
    XModbusReply* reply = XModbusClient_sendRawRequest(client, request, serverAddress);
    XModbusRequest_delete_base(request);
    return reply;
}

XModbusReply* XModbusClient_sendReadWriteRequest(XModbusClient* client, const XModbusDataUnit* read, const XModbusDataUnit* write, int serverAddress) {
    // TODO: Implement ReadWriteMultipleRegisters (FC 0x17)
    // This is a placeholder. You would need to build the specific PDU for FC 0x17.
    (void)client; (void)read; (void)write; (void)serverAddress;
    return NULL;
}

XModbusReply* XModbusClient_sendRawRequest(XModbusClient* client, const XModbusRequest* request, int serverAddress) {
    if (!client || !request || serverAddress < 0) {
        return NULL;
    }

    // Determine reply type
    XModbusReply_ReplyType type = XModbusReply_Raw;
    if (request->m_base.m_code == XModbusPdu_ReadCoils ||
        request->m_base.m_code == XModbusPdu_ReadDiscreteInputs ||
        request->m_base.m_code == XModbusPdu_ReadInputRegisters ||
        request->m_base.m_code == XModbusPdu_ReadHoldingRegisters ||
        request->m_base.m_code == XModbusPdu_WriteSingleCoil ||
        request->m_base.m_code == XModbusPdu_WriteSingleRegister ||
        request->m_base.m_code == XModbusPdu_WriteMultipleCoils ||
        request->m_base.m_code == XModbusPdu_WriteMultipleRegisters) {
        type = XModbusReply_Common;
    }

    XModbusReply* reply = XModbusReply_create(type, serverAddress);
    if (!reply) return NULL;

    // TODO: Here you would typically enqueue the request to be sent by the underlying transport layer (e.g., serial port).
    // For now, we just create the reply object. The actual sending and receiving logic would be in a derived class
    // like XModbusRtuClient or XModbusTcpClient, which overrides the device's event loop.

    // Simulate that the request has been sent and is pending
    // In a real implementation, you'd store this reply in a map keyed by transaction ID or something similar.

    return reply;
}

// --- Configuration Getters/Setters ---
int XModbusClient_timeout(const XModbusClient* client) {
    return client ? client->m_timeout : 1000;
}

void XModbusClient_setTimeout(XModbusClient* client, int newTimeout) {
    if (!client || client->m_timeout == newTimeout) return;
    client->m_timeout = newTimeout;
    XModbusClient_timeoutChanged_signal(client, newTimeout);
}

int XModbusClient_numberOfRetries(const XModbusClient* client) {
    return client ? client->m_numberOfRetries : 3;
}

void XModbusClient_setNumberOfRetries(XModbusClient* client, int number) {
    if (!client) return;
    client->m_numberOfRetries = number;
}

bool XModbusClient_processResponse_base(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data)
{
    if (ISNULL(client, "") || ISNULL(XClassGetVtable(client), ""))
        return false;
    return XClassGetVirtualFunc(client, EXModbusClient_ProcessResponse, bool(*)(const XModbusResponse*, XModbusDataUnit*))(response, data);
}

bool XModbusClient_processPrivateResponse_base(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data)
{
    if (ISNULL(client, "") || ISNULL(XClassGetVtable(client), ""))
        return false;
    return XClassGetVirtualFunc(client, EXModbusClient_ProcessPrivateResponse, bool(*)(const XModbusResponse*, XModbusDataUnit*))(response, data);
}

// ================== Protected API ==================
bool VXModbusClient_processResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data) 
{
    (void)client; // Unused in default impl
    if (!response || !data) return false;

    XModbusPdu_FunctionCode fc = XModbusPdu_functionCode(&response->m_base);
    bool isException = XModbusPdu_isException(&response->m_base);

    // Handle exception responses
    if (isException) {
        // An exception response means the request failed.
        // The data unit should not be modified.
        return false;
    }

    // Handle standard read responses
    if (fc == XModbusPdu_ReadCoils || fc == XModbusPdu_ReadDiscreteInputs) {
        // Byte count is first byte of data
        if (XModbusPdu_dataSize(&response->m_base) < 1) return false;
        uint8_t byteCount = XByteArray_At_Base(response->m_base.m_data, 0);
        if (byteCount == 0) return true; // Valid but empty

        // Set register type
        data->m_type = (fc == XModbusPdu_ReadCoils) ? XModbusCoils : XModbusDiscreteInputs;
        // Start address and count must be known from the original request, not the response.
        // This is a limitation of this simplified design. In practice, you'd store the original
        // request with the reply and use that info here.
        // For demo, we'll just fill the values.
        XModbusDataUnit_setValueCount(data, byteCount * 8); // Max possible bits

        size_t bitIndex = 0;
        for (int i = 1; i <= byteCount && bitIndex < data->m_valueCount; i++) {
            uint8_t byte = XByteArray_At_Base(response->m_base.m_data, i);
            for (int j = 0; j < 8 && bitIndex < data->m_valueCount; j++) {
                bool bitValue = (byte >> j) & 1;
                XModbusDataUnit_setValue(data, bitIndex, bitValue ? 1 : 0);
                bitIndex++;
            }
        }
        return true;
    }

    if (fc == XModbusPdu_ReadInputRegisters || fc == XModbusPdu_ReadHoldingRegisters) {
        if (XModbusPdu_dataSize(&response->m_base) < 1) return false;
        uint8_t byteCount = XByteArray_At_Base(response->m_base.m_data, 0);
        if (byteCount % 2 != 0) return false; // Must be even

        data->m_type = (fc == XModbusPdu_ReadInputRegisters) ? XModbusInputRegisters : XModbusHoldingRegisters;
        XModbusDataUnit_setValueCount(data, byteCount / 2);

        for (size_t i = 0; i < data->m_valueCount; i++) {
            uint16_t reg = (XByteArray_At_Base(response->m_base.m_data, 1 + i * 2) << 8) |
                XByteArray_At_Base(response->m_base.m_data, 1 + i * 2 + 1);
            XModbusDataUnit_setValue(data, i, reg);
        }
        return true;
    }

    // Handle write responses (they usually just echo the request)
    if (fc == XModbusPdu_WriteSingleCoil || fc == XModbusPdu_WriteSingleRegister ||
        fc == XModbusPdu_WriteMultipleCoils || fc == XModbusPdu_WriteMultipleRegisters) {
        // The response confirms the write was successful.
        // The data unit might be filled with the written values, but often it's left as-is.
        // We'll consider it a success.
        return true;
    }

    // If we get here, it's not a standard response, delegate to private handler
    return XModbusClient_processPrivateResponse_base(client, response, data);
}

bool VXModbusClient_processPrivateResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data)
{
    return false;
}


// ================== Signals ==================
void* XModbusClient_timeoutChanged_signal(XModbusClient* client, int newTimeout) {
    XEmitSignal(client, XModbusClient_timeoutChanged_signal, XVariant_create_int(newTimeout), XVariant_delete_base, NULL, XEVENT_PRIORITY_LOWEST);
}