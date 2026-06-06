#include "XModbusServer.h"
#include "XMemory.h"
#include "string.h"

/**
* @brief 处理请求（虚函数）
* @param server XModbusServer实例指针（非NULL）
* @param request 请求PDU
* @return 返回响应PDU指针，需要调用者释放
*/
inline static XModbusResponse* XModbusServer_processRequest_base(XModbusServer* server, const XModbusRequest* request);

/**
* @brief 处理私有请求（虚函数）
* @param server XModbusServer实例指针（非NULL）
* @param request 请求PDU
* @return 返回响应PDU指针，需要调用者释放
*/
inline static XModbusResponse* XModbusServer_processPrivateRequest_base(XModbusServer* server, const XModbusRequest* request);

/**
* @brief 写入数据（虚函数基类实现）
* @param server XModbusServer实例指针（非NULL）
* @param unit 要写入的数据单元
* @return 成功返回true，失败返回false
*/
inline static bool XModbusServer_writeData_base(XModbusServer* server, const XModbusDataUnit* unit);

/**
* @brief 读取数据（虚函数基类实现）
* @param server XModbusServer实例指针（非NULL）
* @param unit 用于存储读取数据的XModbusDataUnit指针
* @return 成功返回true，失败返回false
*/
inline static bool XModbusServer_readData_base(XModbusServer* server, XModbusDataUnit* unit);

// =============== 虚函数前置声明 ===============
static XModbusResponse* VXModbusServer_processRequest(XModbusServer* server, const XModbusRequest* request);
static XModbusResponse* VXModbusServer_processPrivateRequest(XModbusServer* server, const XModbusRequest* request);
static bool VXModbusServer_writeData(XModbusServer* server, const XModbusDataUnit* unit);
static bool VXModbusServer_readData(XModbusServer* server, XModbusDataUnit* unit);
static bool VXModbusServer_setMap(XModbusServer* server, const XModbusDataUnitMap* map);
static bool VXModbusServer_processesBroadcast(const XModbusServer* server);
static XVariant* VXModbusServer_value(const XModbusServer* server, int option);
static bool VXModbusServer_setValue(XModbusServer* server, int option, const XVariant* value);
static void VXModbusServer_deinit(XModbusServer* server);

// =============== 辅助函数 ===============
static inline bool XModbusServer_isBitType(XModbusRegisterType type) {
    return type == XModbusDiscreteInputs || type == XModbusCoils;
}

// =============== 类初始化 ===============
XVtable* XModbusServer_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XModbusServer))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
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

#if SHOWCONTAINERSIZE
    printf("XModbusServer vtable size: %d\n", XVtable_size(XClassVtable));
#endif
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
bool XModbusServer_setMap(XModbusServer* server, const XModbusDataUnitMap* map)
{
    return XModbusServer_setMap_base(server, map);
}

bool XModbusServer_setMap_base(XModbusServer* server, const XModbusDataUnitMap* map)
{
    if (!server) return false;
    return XClassGetVirtualFunc(server, EXModbusServer_SetMap, 
                                 bool (*)(XModbusServer*, const XModbusDataUnitMap*))(server, map);
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
XVariant* XModbusServer_value(const XModbusServer* server, int option)
{
    return XModbusServer_value_base(server, option);
}

bool XModbusServer_setValue(XModbusServer* server, int option, const XVariant* value)
{
    return XModbusServer_setValue_base(server, option, value);
}

XVariant* XModbusServer_value_base(const XModbusServer* server, int option)
{
    if (!server) return NULL;
    return XClassGetVirtualFunc(server, EXModbusServer_Value, 
                                 XVariant* (*)(const XModbusServer*, int))(server, option);
}

bool XModbusServer_setValue_base(XModbusServer* server, int option, const XVariant* value)
{
    if (!server) return false;
    return XClassGetVirtualFunc(server, EXModbusServer_SetValue, 
                                 bool (*)(XModbusServer*, int, const XVariant*))(server, option, value);
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
static XModbusResponse* VXModbusServer_processRequest(XModbusServer* server, const XModbusRequest* request)
{
    if (!server || !request) return NULL;
    
    XModbusPdu_FunctionCode code = XModbusPdu_functionCode((const XModbusPdu*)request);
    
    // 根据功能码处理标准请求
    switch (code) {
        case XModbusPdu_ReadCoils:
        case XModbusPdu_ReadDiscreteInputs:
        case XModbusPdu_ReadHoldingRegisters:
        case XModbusPdu_ReadInputRegisters:
        case XModbusPdu_WriteSingleCoil:
        case XModbusPdu_WriteSingleRegister:
        case XModbusPdu_WriteMultipleCoils:
        case XModbusPdu_WriteMultipleRegisters:
            // 派生类实现具体处理逻辑
            return XModbusResponse_create();
        
        default:
            // 未知功能码，返回异常
            return XModbusResponse_create_with_code(
                (XModbusPdu_FunctionCode)(code | XMODBUS_PDU_EXCEPTION_BYTE));
    }
}

static XModbusResponse* VXModbusServer_processPrivateRequest(XModbusServer* server, const XModbusRequest* request)
{
    if (!server || !request) return NULL;
    
    // 处理私有/自定义请求
    // 派生类可重写此函数
    return XModbusResponse_create();
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

static bool VXModbusServer_setMap(XModbusServer* server, const XModbusDataUnitMap* map)
{
    if (!server || !map) return false;
    
    // 清除旧的数据映射
    if (server->m_dataMap) {
        XModbusDataUnitMap_delete_base(server->m_dataMap);
    }
    
    // 复制新的数据映射
    server->m_dataMap = XMap_create_copy(map);
    return server->m_dataMap != NULL;
}

static bool VXModbusServer_processesBroadcast(const XModbusServer* server)
{
    (void)server;
    return false;  // 默认不处理广播
}

static XVariant* VXModbusServer_value(const XModbusServer* server, int option)
{
    if (!server || !server->m_options) return NULL;
    
    XVariant* found = (XVariant*)XMapBase_value_base(server->m_options, &option);
    if (!found ) return NULL;
    
    // 返回副本
    return XVariant_create_copy(found);
}

static bool VXModbusServer_setValue(XModbusServer* server, int option, const XVariant* value)
{
    if (!server || !server->m_options || !value) return false;
    
    XVariant* newValue = XVariant_create_copy(value);
    if (!newValue) return false;
    
    // 检查是否已存在
    XVariant* existing = (XVariant*)XMapBase_value_base(server->m_options, &option);
    if (existing ) {
        XVariant_delete_base(existing);
    }
    
    // 插入新值
    XMap_insert_move_base(server->m_options, &option, newValue);
    XVariant_delete_base(newValue);
    return true;
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