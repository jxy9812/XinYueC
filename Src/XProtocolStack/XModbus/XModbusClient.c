#include "XModbusClient.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"
#include "XByteArray.h"
#include <string.h>

/******************************************************************************************
 * Protected API (供子类重载)
 ******************************************************************************************/

 /**
 * @brief 处理标准Modbus响应（受保护，供内部调用）
 * @param client 客户端实例指针（非NULL）
 * @param response 收到的响应PDU
 * @param data 用于填充解析结果的数据单元
 * @return 解析成功返回true，失败返回false
 * @note 此为虚函数入口，实际逻辑由子类实现
 */
inline static bool XModbusClient_processResponse_base(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);

/**
* @brief 处理私有/自定义Modbus响应（受保护，供内部调用）
* @param client 客户端实例指针（非NULL）
* @param response 收到的响应PDU
* @param data 用于填充解析结果的数据单元
* @return 解析成功返回true，失败返回false
* @note 此为虚函数入口，实际逻辑由子类实现
*/
inline static bool XModbusClient_processPrivateResponse_base(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);

// 虚函数重载声明
static void VXModbusClient_deinit(XModbusClient* client);
static bool VXModbusClient_processResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);
static bool VXModbusClient_processPrivateResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);
static XModbusReply* VXModbusClient_sendRawRequest(XModbusClient* client, const XModbusRequest* request, int serverAddress);

// ================== 虚表初始化 ==================
XVtable* XModbusClient_class_init(void) 
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XModbusClient))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
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
            XModbusPdu_setData(&req->m_base, XContainerDataAddr(payload), XByteArray_size_base(payload));
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
    XModbusReply* reply = XModbusClient_sendRawRequest_base(client, request, serverAddress);
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
    XModbusReply* reply = XModbusClient_sendRawRequest_base(client, request, serverAddress);
    XModbusRequest_delete_base(request);
    return reply;
}

// ================== 辅助函数：构建读写组合请求PDU (FC 0x17) ==================
/**
* @brief 构建读写组合请求PDU
* @param readUnit 读取数据单元
* @param writeUnit 写入数据单元
* @return 请求PDU，失败返回NULL
*/
static XModbusRequest* buildReadWriteRequest(const XModbusDataUnit* readUnit, const XModbusDataUnit* writeUnit) {
    if (!readUnit || !writeUnit || 
        !XModbusDataUnit_isValid(readUnit) || !XModbusDataUnit_isValid(writeUnit)) {
        return NULL;
    }
    
    // 只支持保持寄存器
    if (readUnit->m_type != XModbusHoldingRegisters || writeUnit->m_type != XModbusHoldingRegisters) {
        return NULL;
    }
    
    size_t readCount = XModbusDataUnit_valueCount(readUnit);
    size_t writeCount = XModbusDataUnit_valueCount(writeUnit);
    
    if (readCount == 0 || readCount > 125 || writeCount == 0 || writeCount > 121) {
        return NULL;  // Modbus规范限制
    }
    
    // 请求格式：
    // 读起始地址(2) + 读取数量(2) + 写起始地址(2) + 写入数量(2) + 字节计数(1) + 写入数据(N)
    uint16_t readStartAddr = (uint16_t)XModbusDataUnit_startAddress(readUnit);
    uint16_t writeStartAddr = (uint16_t)XModbusDataUnit_startAddress(writeUnit);
    uint8_t writeByteCount = (uint8_t)(writeCount * 2);
    
    size_t dataSize = 9 + writeByteCount;
    uint8_t* data = (uint8_t*)XMalloc_System(dataSize);
    if (!data) return NULL;
    
    // 读起始地址
    data[0] = (uint8_t)((readStartAddr >> 8) & 0xFF);
    data[1] = (uint8_t)(readStartAddr & 0xFF);
    // 读取数量
    data[2] = (uint8_t)((readCount >> 8) & 0xFF);
    data[3] = (uint8_t)(readCount & 0xFF);
    // 写起始地址
    data[4] = (uint8_t)((writeStartAddr >> 8) & 0xFF);
    data[5] = (uint8_t)(writeStartAddr & 0xFF);
    // 写入数量
    data[6] = (uint8_t)((writeCount >> 8) & 0xFF);
    data[7] = (uint8_t)(writeCount & 0xFF);
    // 字节计数
    data[8] = writeByteCount;
    
    // 写入数据（大端序）
    for (size_t i = 0; i < writeCount; i++) {
        uint16_t val = (uint16_t)XModbusDataUnit_value(writeUnit, i);
        data[9 + i * 2] = (uint8_t)((val >> 8) & 0xFF);
        data[9 + i * 2 + 1] = (uint8_t)(val & 0xFF);
    }
    
    XModbusRequest* req = XModbusRequest_create_with_code_and_data(XModbusPdu_ReadWriteMultipleRegisters, data, dataSize);
    XFree_System(data);
    return req;
}

XModbusReply* XModbusClient_sendReadWriteRequest(XModbusClient* client, const XModbusDataUnit* read, const XModbusDataUnit* write, int serverAddress) {
    if (!client || !read || !write || serverAddress < 0) {
        return NULL;
    }
    
    XModbusRequest* request = buildReadWriteRequest(read, write);
    if (!request) {
        return NULL;
    }
    
    XModbusReply* reply = XModbusClient_sendRawRequest_base(client, request, serverAddress);
    XModbusRequest_delete_base(request);
    return reply;
}

// ================== sendRawRequest 虚函数实现 ==================

/**
 * @brief 默认的 sendRawRequest 实现
 * @note 基类不提供实际发送功能，子类必须重写此函数
 */
static XModbusReply* VXModbusClient_sendRawRequest(XModbusClient* client, const XModbusRequest* request, int serverAddress) {
    // 基类是抽象的，不提供默认实现
    (void)client; (void)request; (void)serverAddress;
    return NULL;
}

/**
 * @brief 通过虚函数表调用 sendRawRequest
 */
XModbusReply* XModbusClient_sendRawRequest_base(XModbusClient* client, const XModbusRequest* request, int serverAddress) {
    if (!client || !XClassGetVtable(client)) return NULL;
    XModbusReply* (*func)(XModbusClient*, const XModbusRequest*, int) = 
        XClassGetVirtualFunc(client, EXModbusClient_SendRawRequest, 
            XModbusReply*(*)(XModbusClient*, const XModbusRequest*, int));
    if (!func) return NULL;
    return func(client, request, serverAddress);
}

/**
 * @brief 辅助函数：创建并初始化 Reply 对象
 * @note 供子类在实现 sendRawRequest 时调用
 */
XModbusReply* XModbusClient_createReply(XModbusClient* client, const XModbusRequest* request, int serverAddress) {
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
    return XClassGetVirtualFunc(client, EXModbusClient_ProcessResponse, bool(*)(XModbusClient*, const XModbusResponse*, XModbusDataUnit*))(client, response, data);
}

bool XModbusClient_processPrivateResponse_base(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data)
{
    if (ISNULL(client, "") || ISNULL(XClassGetVtable(client), ""))
        return false;
    return XClassGetVirtualFunc(client, EXModbusClient_ProcessPrivateResponse, bool(*)(XModbusClient*, const XModbusResponse*, XModbusDataUnit*))(client, response, data);
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

        if (fc == XModbusPdu_ReadInputRegisters || fc == XModbusPdu_ReadHoldingRegisters ||
        fc == XModbusPdu_ReadWriteMultipleRegisters) {
        if (XModbusPdu_dataSize(&response->m_base) < 1) return false;
        uint8_t byteCount = XByteArray_At_Base(response->m_base.m_data, 0);
        if (byteCount % 2 != 0) return false; // Must be even

        if (fc == XModbusPdu_ReadInputRegisters) {
            data->m_type = XModbusInputRegisters;
        } else {
            data->m_type = XModbusHoldingRegisters;  // ReadHoldingRegisters 和 ReadWriteMultipleRegisters
        }
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
    XEmitSignal(client, XModbusClient_timeoutChanged_signal, 
                XVarList_Create(XVar(int, newTimeout)), 
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}