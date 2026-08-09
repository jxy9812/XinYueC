#include "XModbus_config.h"
#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_SERVER_ON
#include "XModbusServer.h"
#include "XModbusServer_Protected.h"
#include "XMemory.h"
#include "XByteArray.h"
#include "string.h"

// =============== 虚函数前置声明 ===============
static XModbusResponse* VXModbusServer_processRequest(XModbusServer* server, const XModbusRequest* request);
static XModbusResponse* VXModbusServer_processPrivateRequest(XModbusServer* server, const XModbusRequest* request);
static bool VXModbusServer_writeData(XModbusServer* server, const XModbusDataUnit* unit);
static bool VXModbusServer_readData(XModbusServer* server, XModbusDataUnit* unit);
static bool VXModbusServer_setMap(XModbusServer* server,XModbusDataUnitMap* map, XFuncParamType type);
static bool VXModbusServer_processesBroadcast(const XModbusServer* server);
static XVariant* VXModbusServer_value(const XModbusServer* server, int option, XFuncReturnType type);
static bool VXModbusServer_setValue(XModbusServer* server, int option, XVariant* value, XFuncParamType type);
static void VXModbusServer_deinit(XModbusServer* server);

// =============== 辅助函数 ===============
static inline bool XModbusServer_isBitType(XModbusRegisterType type) {
    return type == XModbusDiscreteInputs || type == XModbusCoils;
}

// =============== 类初始化 ===============
XVtable* XModbusServer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusServer)
    // 继承 XModbusDevice
    XVTABLE_INHERIT_XCLASS(XModbusDevice);
    void* table[] = { VXModbusServer_processRequest,VXModbusServer_processPrivateRequest,
    VXModbusServer_writeData ,VXModbusServer_readData,VXModbusServer_setMap ,
    VXModbusServer_processesBroadcast,VXModbusServer_value,VXModbusServer_setValue };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 重载虚函数
   /* XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_ProcessRequest, VXModbusServer_processRequest);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_ProcessPrivateRequest, VXModbusServer_processPrivateRequest);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_WriteData, VXModbusServer_writeData);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_ReadData, VXModbusServer_readData);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_SetMap, VXModbusServer_setMap);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_ProcessesBroadcast, VXModbusServer_processesBroadcast);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_Value, VXModbusServer_value);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_SetValue, VXModbusServer_setValue);*/
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusServer_deinit);

    XCLASS_SHOW_SIZE_DEFAULT(XModbusServer);
    return XVTABLE_DEFAULT;
}

// =============== 创建/初始化 ===============
XModbusServer* XModbusServer_create(void)
{
    XModbusServer* server = XMalloc_System(sizeof(XModbusServer));
    if (!server) return NULL;
    XModbusServer_init(server);
    Set_Class_MemoryFree(server, XFree_System);
    return server;
}

void XModbusServer_init(XModbusServer* server)
{
    if (!server) return;
    XModbusDevice_init(&server->m_base);
    XClassGetVtable(server) = XModbusServer_class_init();
    
    server->m_serverAddress = 1;
    server->m_dataMap = XModbusDataUnitMap_create();
    server->m_options = XMap_Create(int, XVariant, int_compare);
    if (server->m_options) {
        XContainerSetDataCopyMethod(server->m_options, XVariant_copy_base);
        XContainerSetDataMoveMethod(server->m_options, XVariant_move_base);
        XContainerSetDataDeinitMethod(server->m_options, XVariant_deinit_base);
    }
}

// =============== 服务器地址 ===============
int XModbusServer_serverAddress(const XModbusServer* server)
{
    return server ? server->m_serverAddress : -1;
}

void XModbusServer_setServerAddress(XModbusServer* server, int address)
{
    if (server) {
        server->m_serverAddress = address;
    }
}

// =============== 数据映射表 ===============

bool XModbusServer_setMap_base(XModbusServer* server, const XModbusDataUnitMap* map)
{
    if (!server || !XClassGetVtable(server)) return false;
    return XClassGetVirtualFunc(server, EXModbusServer_SetMap, 
                                 bool (*)(XModbusServer*, const XModbusDataUnitMap*, XFuncParamType type))(server, map, XFuncParamType_Copy);
}

bool XModbusServer_setMap_move_base(XModbusServer* server, XModbusDataUnitMap* map)
{
    if (!server || !XClassGetVtable(server)) return false;
    return XClassGetVirtualFunc(server, EXModbusServer_SetMap,
        bool (*)(XModbusServer*,XModbusDataUnitMap*, XFuncParamType type))(server, map, XFuncParamType_Move);
}

bool XModbusServer_setMap_ref_base(XModbusServer* server, XModbusDataUnitMap* map)
{
    if (!server || !XClassGetVtable(server)) return false;
    return XClassGetVirtualFunc(server, EXModbusServer_SetMap,
        bool (*)(XModbusServer*,XModbusDataUnitMap*, XFuncParamType type))(server, map, XFuncParamType_Ref);
}

// =============== 数据读取 ===============
bool XModbusServer_data1(const XModbusServer* server, XModbusDataUnit* unit)
{
    return XModbusServer_readData_base((XModbusServer*)server, unit);
}

bool XModbusServer_data2(const XModbusServer* server, XModbusRegisterType table, 
                          uint16_t address, uint16_t* value)
{
    if (!server || !value) return false;
    
    // 创建临时数据单元用于读取
    XModbusDataUnit* unit = XModbusDataUnit_create_ex(table, address, 1);
    if (!unit) return false;
    
    bool result = XModbusServer_readData_base((XModbusServer*)server, unit);
    if (result) {
        *value = XModbusDataUnit_value(unit, 0);
    }
    
    XModbusDataUnit_delete_base(unit);
    return result;
}

// =============== 数据写入 ===============
bool XModbusServer_setData1(XModbusServer* server, const XModbusDataUnit* unit)
{
    return XModbusServer_writeData_base(server, unit);
}

bool XModbusServer_setData2(XModbusServer* server, XModbusRegisterType table, 
                             uint16_t address, uint16_t value)
{
    if (!server) return false;
    
    // 创建临时数据单元用于写入
    XModbusDataUnit* unit = XModbusDataUnit_create_ex(table, address, 1);
    if (!unit) return false;
    
    XModbusDataUnit_setValue(unit, 0, value);
    bool result = XModbusServer_writeData_base(server, unit);
    
    XModbusDataUnit_delete_base(unit);
    return result;
}

// =============== 选项管理 ===============

XVariant* XModbusServer_value_base(const XModbusServer* server, int option)
{
    if (!server || !XClassGetVtable(server)) return NULL;
    return XClassGetVirtualFunc(server, EXModbusServer_Value, 
                                 XVariant* (*)(const XModbusServer*, int, XFuncReturnType))(server, option, XFuncReturnType_Copy);
}

const XVariant* XModbusServer_value_const_base(const XModbusServer* server, int option)
{
    if (!server || !XClassGetVtable(server)) return NULL;
    return XClassGetVirtualFunc(server, EXModbusServer_Value,
        XVariant * (*)(const XModbusServer*, int, XFuncReturnType))(server, option, XFuncReturnType_Ref);
}

bool XModbusServer_setValue_base(XModbusServer* server, int option, const XVariant* value)
{
    if (!server || !XClassGetVtable(server)) return false;
    return XClassGetVirtualFunc(server, EXModbusServer_SetValue, 
                                 bool (*)(XModbusServer*, int, const XVariant*, XFuncParamType))(server, option, value, XFuncParamType_Copy);
}

bool XModbusServer_setValue_move_base(XModbusServer* server, int option,XVariant* value)
{
    if (!server || !XClassGetVtable(server)) return false;
    return XClassGetVirtualFunc(server, EXModbusServer_SetValue,
        bool (*)(XModbusServer*, int, XVariant*, XFuncParamType))(server, option, value, XFuncParamType_Move);
}

// =============== 广播处理 ===============
bool XModbusServer_processesBroadcast(const XModbusServer* server)
{
    return XModbusServer_processesBroadcast_base(server);
}

bool XModbusServer_processesBroadcast_base(const XModbusServer* server)
{
    if (!server) return false;
    return XClassGetVirtualFunc(server, EXModbusServer_ProcessesBroadcast, 
                                 bool (*)(const XModbusServer*))(server);
}

// =============== 请求处理 ===============
XModbusResponse* XModbusServer_processRequest_base(XModbusServer* server, const XModbusRequest* request)
{
    if (!server || !request) return NULL;
    
    return XClassGetVirtualFunc(server, EXModbusServer_ProcessRequest, 
                                 XModbusResponse* (*)(XModbusServer*, const XModbusRequest*))(server, request);
}

XModbusResponse* XModbusServer_processPrivateRequest_base(XModbusServer* server, const XModbusRequest* request)
{
    if (!server || !request) return NULL;
    
    return XClassGetVirtualFunc(server, EXModbusServer_ProcessPrivateRequest, 
                                 XModbusResponse* (*)(XModbusServer*, const XModbusRequest*))(server, request);
}

// =============== 虚函数默认实现 ===============
bool XModbusServer_writeData_base(XModbusServer* server, const XModbusDataUnit* unit)
{
    if (!server) return false;
    return XClassGetVirtualFunc(server, EXModbusServer_WriteData, 
                                 bool (*)(XModbusServer*, const XModbusDataUnit*))(server, unit);
}

bool XModbusServer_readData_base(XModbusServer* server, XModbusDataUnit* unit)
{
    if (!server) return false;
    return XClassGetVirtualFunc(server, EXModbusServer_ReadData, 
                                 bool (*)(XModbusServer*, XModbusDataUnit*))(server, unit);
}

// =============== 虚函数实现 ===============
// =============== 辅助函数：创建异常响应 ===============
static XModbusResponse* createExceptionResponse(XModbusPdu_FunctionCode fc, XModbusPdu_ExceptionCode ec)
{
    XModbusExceptionResponse* exc = XModbusExceptionResponse_create_with_function_and_exception(fc, ec);
    return (XModbusResponse*)exc;
}

// =============== 辅助函数：从PDU数据读取uint16（大端序）==============
static inline uint16_t readUint16FromData(const uint8_t* data, size_t offset)
{
    uint16_t value;
    XMemory_read_data(data + offset, XBYTE_ORDER_BIG_ENDIAN, (uint8_t*)&value, sizeof(uint16_t));
    return value;
}

// =============== 辅助函数：写入uint16到PDU数据（大端序）==============
static inline void writeUint16ToData(uint8_t* data, size_t offset, uint16_t value)
{
    XMemory_write_data(data + offset, XBYTE_ORDER_BIG_ENDIAN, (const uint8_t*)&value, sizeof(uint16_t));
}

// =============== 完整的 processRequest 实现 ===============
static XModbusResponse* VXModbusServer_processRequest(XModbusServer* server, const XModbusRequest* request)
{
    if (!server || !request) {
        return NULL;
    }

    XModbusPdu_FunctionCode code = XModbusPdu_functionCode((const XModbusPdu*)request);
    const XByteArray* reqData = ((const XModbusPdu*)request)->m_data;
    const uint8_t* data = XContainerDataAddr(reqData);
    size_t dataSize = XByteArray_size_base(reqData);

    // 检查数据映射表是否存在
    if (!server->m_dataMap) {
        return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
    }

    // =============== FC01: 读线圈 ===============
    if (code == XModbusPdu_ReadCoils) {
        if (dataSize < 4) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint16_t startAddress = readUint16FromData(data, 0);
        uint16_t quantity = readUint16FromData(data, 2);

        if (quantity < 1 || quantity > 2000) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        XModbusDataUnit* unit = XModbusDataUnit_create_ex(XModbusCoils, startAddress, quantity);
        if (!unit) {
            return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
        }

        if (!XModbusServer_readData_base(server, unit)) {
            XModbusDataUnit_delete_base(unit);
            return createExceptionResponse(code, XModbusPdu_IllegalDataAddress);
        }

        uint8_t byteCount = (quantity + 7) / 8;
        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XByteArray* respData = ((XModbusPdu*)response)->m_data;

        XByteArray_push_back_1(respData, byteCount);

        for (uint16_t i = 0; i < quantity; ) {
            uint8_t coilByte = 0;
            for (int bit = 0; bit < 8 && i < quantity; bit++, i++) {
                if (XModbusDataUnit_value(unit, i)) {
                    coilByte |= (1 << bit);
                }
            }
            XByteArray_push_back_1(respData, coilByte);
        }

        XModbusDataUnit_delete_base(unit);
        return response;
    }

    // =============== FC02: 读离散输入 ===============
    if (code == XModbusPdu_ReadDiscreteInputs) {
        if (dataSize < 4) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint16_t startAddress = readUint16FromData(data, 0);
        uint16_t quantity = readUint16FromData(data, 2);

        if (quantity < 1 || quantity > 2000) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        XModbusDataUnit* unit = XModbusDataUnit_create_ex(XModbusDiscreteInputs, startAddress, quantity);
        if (!unit) {
            return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
        }

        if (!XModbusServer_readData_base(server, unit)) {
            XModbusDataUnit_delete_base(unit);
            return createExceptionResponse(code, XModbusPdu_IllegalDataAddress);
        }

        uint8_t byteCount = (quantity + 7) / 8;
        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XByteArray* respData = ((XModbusPdu*)response)->m_data;

        XByteArray_push_back_1(respData, byteCount);

        for (uint16_t i = 0; i < quantity; ) {
            uint8_t inputByte = 0;
            for (int bit = 0; bit < 8 && i < quantity; bit++, i++) {
                if (XModbusDataUnit_value(unit, i)) {
                    inputByte |= (1 << bit);
                }
            }
            XByteArray_push_back_1(respData, inputByte);
        }

        XModbusDataUnit_delete_base(unit);
        return response;
    }

    // =============== FC03: 读保持寄存器 ===============
    if (code == XModbusPdu_ReadHoldingRegisters) {
        if (dataSize < 4) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint16_t startAddress = readUint16FromData(data, 0);
        uint16_t quantity = readUint16FromData(data, 2);

        if (quantity < 1 || quantity > 125) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        XModbusDataUnit* unit = XModbusDataUnit_create_ex(XModbusHoldingRegisters, startAddress, quantity);
        if (!unit) {
            return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
        }

        if (!XModbusServer_readData_base(server, unit)) {
            XModbusDataUnit_delete_base(unit);
            return createExceptionResponse(code, XModbusPdu_IllegalDataAddress);
        }

        uint8_t byteCount = (uint8_t)(quantity * 2);
        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XByteArray* respData = ((XModbusPdu*)response)->m_data;

        XByteArray_push_back_1(respData, byteCount);

        // 使用 XMemory_write_data 写入大端序数据
        for (uint16_t i = 0; i < quantity; i++) {
            uint16_t reg = XModbusDataUnit_value(unit, i);
            uint8_t regBytes[2];
            XMemory_write_data(regBytes, XBYTE_ORDER_BIG_ENDIAN, (const uint8_t*)&reg, 2);
            XByteArray_push_back_2(respData, regBytes, 2);
        }

        XModbusDataUnit_delete_base(unit);
        return response;
    }

    // =============== FC04: 读输入寄存器 ===============
    if (code == XModbusPdu_ReadInputRegisters) {
        if (dataSize < 4) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint16_t startAddress = readUint16FromData(data, 0);
        uint16_t quantity = readUint16FromData(data, 2);

        if (quantity < 1 || quantity > 125) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        XModbusDataUnit* unit = XModbusDataUnit_create_ex(XModbusInputRegisters, startAddress, quantity);
        if (!unit) {
            return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
        }

        if (!XModbusServer_readData_base(server, unit)) {
            XModbusDataUnit_delete_base(unit);
            return createExceptionResponse(code, XModbusPdu_IllegalDataAddress);
        }

        uint8_t byteCount = (uint8_t)(quantity * 2);
        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XByteArray* respData = ((XModbusPdu*)response)->m_data;

        XByteArray_push_back_1(respData, byteCount);

        for (uint16_t i = 0; i < quantity; i++) {
            uint16_t reg = XModbusDataUnit_value(unit, i);
            uint8_t regBytes[2];
            XMemory_write_data(regBytes, XBYTE_ORDER_BIG_ENDIAN, (const uint8_t*)&reg, 2);
            XByteArray_push_back_2(respData, regBytes, 2);
        }

        XModbusDataUnit_delete_base(unit);
        return response;
    }

    // =============== FC05: 写单个线圈 ===============
    if (code == XModbusPdu_WriteSingleCoil) {
        if (dataSize < 4) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint16_t address = readUint16FromData(data, 0);
        uint16_t value = readUint16FromData(data, 2);

        if (value != 0x0000 && value != 0xFF00) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        XModbusDataUnit* unit = XModbusDataUnit_create_ex(XModbusCoils, address, 1);
        if (!unit) {
            return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
        }

        XModbusDataUnit_setValue(unit, 0, (value == 0xFF00) ? 1 : 0);

        if (!XModbusServer_writeData_base(server, unit)) {
            XModbusDataUnit_delete_base(unit);
            return createExceptionResponse(code, XModbusPdu_IllegalDataAddress);
        }

        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XModbusPdu_setData((XModbusPdu*)response, data, 4);

        XModbusDataUnit_delete_base(unit);
        return response;
    }

    // =============== FC06: 写单个寄存器 ===============
    if (code == XModbusPdu_WriteSingleRegister) {
        if (dataSize < 4) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint16_t address = readUint16FromData(data, 0);
        uint16_t value = readUint16FromData(data, 2);

        XModbusDataUnit* unit = XModbusDataUnit_create_ex(XModbusHoldingRegisters, address, 1);
        if (!unit) {
            return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
        }

        XModbusDataUnit_setValue(unit, 0, value);

        if (!XModbusServer_writeData_base(server, unit)) {
            XModbusDataUnit_delete_base(unit);
            return createExceptionResponse(code, XModbusPdu_IllegalDataAddress);
        }

        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XModbusPdu_setData((XModbusPdu*)response, data, 4);

        XModbusDataUnit_delete_base(unit);
        return response;
    }

    // =============== FC07: 读异常状态 ===============
    if (code == XModbusPdu_ReadExceptionStatus) {
        const XVariant* offsetVar = XModbusServer_value_const_base(server, XModbusServer_ExceptionStatusOffset);
        uint16_t offset = offsetVar ? (uint16_t)XVariant_toInt(offsetVar) : 0;

        XModbusDataUnit* unit = XModbusDataUnit_create_ex(XModbusCoils, offset, 8);
        if (!unit) {
            return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
        }

        uint8_t status = 0;
        if (XModbusServer_readData_base(server, unit)) {
            for (int i = 0; i < 8; i++) {
                if (XModbusDataUnit_value(unit, i)) {
                    status |= (1 << i);
                }
            }
        }

        XModbusDataUnit_delete_base(unit);

        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XModbusPdu_setData((XModbusPdu*)response, &status, 1);
        return response;
    }

    // =============== FC08: 诊断 ===============
    if (code == XModbusPdu_Diagnostics) {
        if (dataSize < 4) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint16_t subFunc = readUint16FromData(data, 0);
        uint16_t dataValue = readUint16FromData(data, 2);

        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XByteArray* respData = ((XModbusPdu*)response)->m_data;

        switch (subFunc) {
        case 0x0001: // 返回查询数据
        case 0x0002: // 重置通信链路
        case 0x0003: // 返回诊断寄存器
        case 0x0004: // 强制监听模式
        case 0x000A: // 清除计数器
        case 0x000B: // 返回总线消息计数
        case 0x000C: // 返回总线通信错误计数
        case 0x000D: // 返回总线异常错误计数
        case 0x000E: // 返回从站消息计数
        case 0x000F: // 返回从站无响应计数
        case 0x0010: // 返回从站NAK计数
        case 0x0011: // 返回从站忙计数
        case 0x0012: // 返回总线字符溢出计数
        {
            uint8_t respBytes[4];
            writeUint16ToData(respBytes, 0, subFunc);
            writeUint16ToData(respBytes, 2, dataValue);
            XByteArray_push_back_2(respData, respBytes, 4);
        }
        break;

        default:
            XModbusResponse_delete_base(response);
            return createExceptionResponse(code, XModbusPdu_IllegalFunction);
        }
        return response;
    }

    // =============== FC11: 获取通信事件计数器 ===============
    if (code == XModbusPdu_GetCommEventCounter) {
        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XByteArray* respData = ((XModbusPdu*)response)->m_data;

        uint8_t respBytes[4];
        writeUint16ToData(respBytes, 0, 0xFFFF); // 状态
        writeUint16ToData(respBytes, 2, 0x0000); // 事件计数
        XByteArray_push_back_2(respData, respBytes, 4);
        return response;
    }

    // =============== FC12: 获取通信事件日志 ===============
    if (code == XModbusPdu_GetCommEventLog) {
        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XByteArray* respData = ((XModbusPdu*)response)->m_data;

        XByteArray_push_back_1(respData, 6); // 字节计数

        uint8_t respBytes[6];
        writeUint16ToData(respBytes, 0, 0xFFFF); // 状态
        writeUint16ToData(respBytes, 2, 0x0000); // 事件计数
        writeUint16ToData(respBytes, 4, 0x0000); // 消息计数
        XByteArray_push_back_2(respData, respBytes, 6);
        return response;
    }

    // =============== FC15: 写多个线圈 ===============
    if (code == XModbusPdu_WriteMultipleCoils) {
        if (dataSize < 5) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint16_t startAddress = readUint16FromData(data, 0);
        uint16_t quantity = readUint16FromData(data, 2);
        uint8_t byteCount = data[4];

        if (quantity < 1 || quantity > 1968) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint8_t expectedBytes = (quantity + 7) / 8;
        if (byteCount != expectedBytes || dataSize < (size_t)(5 + byteCount)) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        XModbusDataUnit* unit = XModbusDataUnit_create_ex(XModbusCoils, startAddress, quantity);
        if (!unit) {
            return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
        }

        for (uint16_t i = 0; i < quantity; i++) {
            uint8_t byteIndex = 5 + (i / 8);
            uint8_t bitIndex = i % 8;
            uint8_t coilValue = (data[byteIndex] >> bitIndex) & 0x01;
            XModbusDataUnit_setValue(unit, i, coilValue);
        }

        if (!XModbusServer_writeData_base(server, unit)) {
            XModbusDataUnit_delete_base(unit);
            return createExceptionResponse(code, XModbusPdu_IllegalDataAddress);
        }

        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XByteArray* respData = ((XModbusPdu*)response)->m_data;

        uint8_t respBytes[4];
        writeUint16ToData(respBytes, 0, startAddress);
        writeUint16ToData(respBytes, 2, quantity);
        XByteArray_push_back_2(respData, respBytes, 4);

        XModbusDataUnit_delete_base(unit);
        return response;
    }

    // =============== FC16: 写多个寄存器 ===============
    if (code == XModbusPdu_WriteMultipleRegisters) {
        if (dataSize < 5) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint16_t startAddress = readUint16FromData(data, 0);
        uint16_t quantity = readUint16FromData(data, 2);
        uint8_t byteCount = data[4];

        if (quantity < 1 || quantity > 123) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        if (byteCount != quantity * 2 || dataSize < (size_t)(5 + byteCount)) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        XModbusDataUnit* unit = XModbusDataUnit_create_ex(XModbusHoldingRegisters, startAddress, quantity);
        if (!unit) {
            return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
        }

        for (uint16_t i = 0; i < quantity; i++) {
            uint16_t regValue = readUint16FromData(data, 5 + i * 2);
            XModbusDataUnit_setValue(unit, i, regValue);
        }

        if (!XModbusServer_writeData_base(server, unit)) {
            XModbusDataUnit_delete_base(unit);
            return createExceptionResponse(code, XModbusPdu_IllegalDataAddress);
        }

        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XByteArray* respData = ((XModbusPdu*)response)->m_data;

        uint8_t respBytes[4];
        writeUint16ToData(respBytes, 0, startAddress);
        writeUint16ToData(respBytes, 2, quantity);
        XByteArray_push_back_2(respData, respBytes, 4);

        XModbusDataUnit_delete_base(unit);
        return response;
    }

    // =============== FC17: 报告服务器ID ===============
    if (code == XModbusPdu_ReportServerId) {
        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XByteArray* respData = ((XModbusPdu*)response)->m_data;

        const XVariant* idVar = XModbusServer_value_const_base(server, XModbusServer_ServerIdentifier);
        uint8_t serverId = idVar ? (uint8_t)XVariant_toInt(idVar) : 0x0A;

        const XVariant* runVar = XModbusServer_value_const_base(server, XModbusServer_RunIndicatorStatus);
        uint8_t runStatus = runVar ? (uint8_t)XVariant_toInt(runVar) : 0xFF;

        const XVariant* addDataVar = XModbusServer_value_const_base(server, XModbusServer_AdditionalData);
        const char* addDataStr = "XModbus Server";
        size_t addDataLen = 14;

        if (addDataVar) {
            const XString* addStr = XVariant_toString_const(addDataVar);
            if (addStr) {
                addDataStr = XString_toUtf8(addStr);
                addDataLen = XString_size_base(addStr);
                if (addDataLen > 249) addDataLen = 249;
            }
        }

        uint8_t byteCount = (uint8_t)(2 + addDataLen);
        XByteArray_push_back_1(respData, byteCount);
        XByteArray_push_back_1(respData, serverId);
        XByteArray_push_back_1(respData, runStatus);
        XByteArray_push_back_2(respData, (const uint8_t*)addDataStr, addDataLen);

        return response;
    }

    // =============== FC22: 掩码写寄存器 ===============
    if (code == XModbusPdu_MaskWriteRegister) {
        if (dataSize < 8) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint16_t address = readUint16FromData(data, 0);
        uint16_t andMask = readUint16FromData(data, 2);
        uint16_t orMask = readUint16FromData(data, 4);

        XModbusDataUnit* unit = XModbusDataUnit_create_ex(XModbusHoldingRegisters, address, 1);
        if (!unit) {
            return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
        }

        if (!XModbusServer_readData_base(server, unit)) {
            XModbusDataUnit_delete_base(unit);
            return createExceptionResponse(code, XModbusPdu_IllegalDataAddress);
        }

        uint16_t currentValue = XModbusDataUnit_value(unit, 0);
        uint16_t newValue = (currentValue & andMask) | (orMask & ~andMask);

        XModbusDataUnit_setValue(unit, 0, newValue);

        if (!XModbusServer_writeData_base(server, unit)) {
            XModbusDataUnit_delete_base(unit);
            return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
        }

        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XModbusPdu_setData((XModbusPdu*)response, data, 8);

        XModbusDataUnit_delete_base(unit);
        return response;
    }

    // =============== FC23: 读写多个寄存器 ===============
    if (code == XModbusPdu_ReadWriteMultipleRegisters) {
        if (dataSize < 9) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint16_t readStartAddr = readUint16FromData(data, 0);
        uint16_t readQuantity = readUint16FromData(data, 2);
        uint16_t writeStartAddr = readUint16FromData(data, 4);
        uint16_t writeQuantity = readUint16FromData(data, 6);
        uint8_t writeByteCount = data[8];

        if (readQuantity < 1 || readQuantity > 125) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        if (writeQuantity < 1 || writeQuantity > 121) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        if (writeByteCount != writeQuantity * 2 || dataSize < (size_t)(9 + writeByteCount)) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        // 先执行写入
        if (writeQuantity > 0) {
            XModbusDataUnit* writeUnit = XModbusDataUnit_create_ex(XModbusHoldingRegisters, writeStartAddr, writeQuantity);
            if (!writeUnit) {
                return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
            }

            for (uint16_t i = 0; i < writeQuantity; i++) {
                uint16_t regValue = readUint16FromData(data, 9 + i * 2);
                XModbusDataUnit_setValue(writeUnit, i, regValue);
            }

            if (!XModbusServer_writeData_base(server, writeUnit)) {
                XModbusDataUnit_delete_base(writeUnit);
                return createExceptionResponse(code, XModbusPdu_IllegalDataAddress);
            }
            XModbusDataUnit_delete_base(writeUnit);
        }

        // 执行读取
        XModbusDataUnit* readUnit = XModbusDataUnit_create_ex(XModbusHoldingRegisters, readStartAddr, readQuantity);
        if (!readUnit) {
            return createExceptionResponse(code, XModbusPdu_ServerDeviceFailure);
        }

        if (!XModbusServer_readData_base(server, readUnit)) {
            XModbusDataUnit_delete_base(readUnit);
            return createExceptionResponse(code, XModbusPdu_IllegalDataAddress);
        }

        uint8_t respByteCount = (uint8_t)(readQuantity * 2);
        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XByteArray* respData = ((XModbusPdu*)response)->m_data;

        XByteArray_push_back_1(respData, respByteCount);

        for (uint16_t i = 0; i < readQuantity; i++) {
            uint16_t reg = XModbusDataUnit_value(readUnit, i);
            uint8_t regBytes[2];
            XMemory_write_data(regBytes, XBYTE_ORDER_BIG_ENDIAN, (const uint8_t*)&reg, 2);
            XByteArray_push_back_2(respData, regBytes, 2);
        }

        XModbusDataUnit_delete_base(readUnit);
        return response;
    }

    // =============== FC24: 读FIFO队列 ===============
    if (code == XModbusPdu_ReadFifoQueue) {
        if (dataSize < 2) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint16_t fifoAddress = readUint16FromData(data, 0);

        if (fifoAddress > 0xFFFF) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataAddress);
        }

        XModbusResponse* response = XModbusResponse_create_with_code(code);
        XByteArray* respData = ((XModbusPdu*)response)->m_data;

        // 字节计数 (2字节) + FIFO计数 (2字节)
        uint8_t respBytes[4];
        writeUint16ToData(respBytes, 0, 2);  // 字节计数
        writeUint16ToData(respBytes, 2, 0);  // FIFO计数
        XByteArray_push_back_2(respData, respBytes, 4);

        return response;
    }

    // =============== FC43: 封装接口传输 (读设备标识) ===============
    if (code == XModbusPdu_EncapsulatedInterfaceTransport) {
        if (dataSize < 3) {
            return createExceptionResponse(code, XModbusPdu_IllegalDataValue);
        }

        uint8_t meiType = data[0];
        uint8_t readDeviceIdCode = data[1];
        uint8_t objectId = data[2];

        if (meiType == 0x0E) {
            const XVariant* idVar = XModbusServer_value_const_base(server, XModbusServer_DeviceIdentification);
            (void)idVar;

            XModbusResponse* response = XModbusResponse_create_with_code(code);
            XByteArray* respData = ((XModbusPdu*)response)->m_data;

            XByteArray_push_back_1(respData, 0x0E);
            XByteArray_push_back_1(respData, readDeviceIdCode);
            XByteArray_push_back_1(respData, 0x83);
            XByteArray_push_back_1(respData, 0x00);
            XByteArray_push_back_1(respData, 0x00);
            XByteArray_push_back_1(respData, 0x03);

            // 对象0: VendorName
            XByteArray_push_back_1(respData, 0x00);
            XByteArray_push_back_1(respData, 0x04);
            XByteArray_push_back_2(respData, (const uint8_t*)"XLib", 4);

            // 对象1: ProductCode
            XByteArray_push_back_1(respData, 0x01);
            XByteArray_push_back_1(respData, 0x04);
            XByteArray_push_back_2(respData, (const uint8_t*)"X001", 4);

            // 对象2: MajorMinorRevision
            XByteArray_push_back_1(respData, 0x02);
            XByteArray_push_back_1(respData, 0x05);
            XByteArray_push_back_2(respData, (const uint8_t*)"1.0.0", 5);

            return response;
        }

        return createExceptionResponse(code, XModbusPdu_IllegalFunction);
    }

    // =============== 其他功能码: 转发给 processPrivateRequest ===============
    return XModbusServer_processPrivateRequest_base(server, request);
}

// =============== processPrivateRequest 实现 ===============
static XModbusResponse* VXModbusServer_processPrivateRequest(XModbusServer* server, const XModbusRequest* request)
{
    if (!server || !request) return NULL;

    // 默认返回非法功能码异常
    XModbusPdu_FunctionCode code = XModbusPdu_functionCode((const XModbusPdu*)request);
    return createExceptionResponse(code, XModbusPdu_IllegalFunction);
}

static bool VXModbusServer_writeData(XModbusServer* server, const XModbusDataUnit* unit)
{
    if (!server || !unit || !server->m_dataMap) return false;
    if (!XModbusDataUnit_isValid(unit)) return false;
    
    XModbusRegisterType type = XModbusDataUnit_registerType(unit);
    
    // 从映射表获取对应类型的数据单元
    XModbusDataUnit* target = (XModbusDataUnit*)XMapBase_value_base(server->m_dataMap, &type);
    if (!target) return false;
    
    // 检查地址范围
    uint16_t startAddr = (uint16_t)XModbusDataUnit_startAddress(unit);
    uint16_t targetStart = (uint16_t)XModbusDataUnit_startAddress(target);
    size_t targetCount = XModbusDataUnit_valueCount(target);
    size_t writeCount = XModbusDataUnit_valueCount(unit);
    
    if (startAddr < targetStart || startAddr + writeCount > targetStart + targetCount) {
        return false;  // 地址超出范围
    }
    
    // 写入数据
    size_t offset = startAddr - targetStart;
    for (size_t i = 0; i < writeCount; i++) {
        uint16_t value = XModbusDataUnit_value(unit, i);
        XModbusDataUnit_setValue(target, offset + i, value);
    }
    
    // 发送数据写入信号
    XModbusServer_dataWritten_signal(server, type, startAddr, (int)writeCount);
    
    return true;
}

static bool VXModbusServer_readData(XModbusServer* server, XModbusDataUnit* unit)
{
    if (!server || !unit || !server->m_dataMap) return false;
    if (!XModbusDataUnit_isValid(unit)) return false;
    
    XModbusRegisterType type = XModbusDataUnit_registerType(unit);
    
    // 从映射表获取对应类型的数据单元
    XModbusDataUnit* source = (XModbusDataUnit*)XMapBase_value_base(server->m_dataMap, &type);
    if (!source) return false;
    
    // 检查地址范围
    uint16_t startAddr = (uint16_t)XModbusDataUnit_startAddress(unit);
    uint16_t sourceStart = (uint16_t)XModbusDataUnit_startAddress(source);
    size_t sourceCount = XModbusDataUnit_valueCount(source);
    size_t readCount = XModbusDataUnit_valueCount(unit);
    
    if (startAddr < sourceStart || startAddr + readCount > sourceStart + sourceCount) {
        return false;  // 地址超出范围
    }
    
    // 读取数据
    size_t offset = startAddr - sourceStart;
    for (size_t i = 0; i < readCount; i++) {
        uint16_t value = XModbusDataUnit_value(source, offset + i);
        XModbusDataUnit_setValue(unit, i, value);
    }
    
    return true;
}

static bool VXModbusServer_setMap(XModbusServer* server, XModbusDataUnitMap* map, XFuncParamType type)
{
    if (!server || !map) return false;
    
    if (type == XFuncParamType_Copy)
    {
        if (server->m_dataMap)
            XMap_copy_base(server->m_dataMap, map);
        else
            server->m_dataMap = XMap_create_copy(map);
    }
    else  if (type == XFuncParamType_Move)
    {
        if (server->m_dataMap)
            XMap_move_base(server->m_dataMap, map);
        else
            server->m_dataMap = XMap_create_move(map);
    }
    else  if (type == XFuncParamType_Ref)
    {
        if (server->m_dataMap)
            XModbusDataUnitMap_delete_base(server->m_dataMap);
        server->m_dataMap = map;
    }
    return server->m_dataMap != NULL;
}

static bool VXModbusServer_processesBroadcast(const XModbusServer* server)
{
    (void)server;
    return false;  // 默认不处理广播
}

static XVariant* VXModbusServer_value(const XModbusServer* server, int option, XFuncReturnType type)
{
    if (!server || !server->m_options) return NULL;
    
    XVariant* found = (XVariant*)XMapBase_value_base(server->m_options, &option);
    if (!found ) return NULL;
  
    // 返回副本
    if(type== XFuncReturnType_Copy)
        return XVariant_create_copy(found);
    if (type == XFuncReturnType_Move)
        return XVariant_create_move(found);
    if (type == XFuncReturnType_Ref)
        return found;
    return XVariant_create_copy(found);
}

static bool VXModbusServer_setValue(XModbusServer* server, int option,XVariant* value, XFuncParamType type)
{
    if (!server || !server->m_options || !value) return false;
    
    // 检查是否已存在
    XVariant* existing = (XVariant*)XMapBase_value_base(server->m_options, &option);
    if (existing ) {
        XVariant_delete_base(existing);
    }
    
    // 插入新值
    if (type == XFuncParamType_Copy)
        return XMap_insert_base(server->m_options, &option, value);
    else if (type == XFuncParamType_Move)
        return XMap_insert_move_base(server->m_options, &option, value);
    return false;
}

static void VXModbusServer_deinit(XModbusServer* server)
{
    if (!server) return;
    
    // 释放数据映射表
    if (server->m_dataMap) {
        XModbusDataUnitMap_delete_base(server->m_dataMap);
        server->m_dataMap = NULL;
    }
    
    // 释放选项映射表
    if (server->m_options) {
        XMapBase_delete_base(server->m_options);
        server->m_options = NULL;
    }
    
    server->m_serverAddress = 0;
}

// =============== 信号 ===============
void* XModbusServer_dataWritten_signal(XModbusServer* server, XModbusRegisterType table, int address, int size)
{
    XEmitSignal(server, XModbusServer_dataWritten_signal, 
                XVarList_Create(XVar(XModbusRegisterType, table), XVar(int, address), XVar(int, size)), 
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XMODBUS_SERVER_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */
