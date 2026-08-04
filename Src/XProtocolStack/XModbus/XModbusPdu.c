#include "XModbusPdu.h"
#include "XMemory.h"
#include "XByteArray.h"
#include "XAlgorithm.h"
#include <string.h>

// 虚函数重载
static void VXModbusPdu_deinit(XModbusPdu* pdu);
static void VXModbusPdu_copy(XModbusPdu* pdu, XModbusPdu* src);
static void VXModbusPdu_move(XModbusPdu* pdu, XModbusPdu* src);
XVtable* XModbusPdu_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XClass)
	XCLASS_SET_CLASS_NAME_DEFAULT("XModbusPdu");
        // 继承 XModbusDevice
        XVTABLE_INHERIT_XCLASS(XClass);
    // 重载析构函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusPdu_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXModbusPdu_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXModbusPdu_move);
	XCLASS_SHOW_SIZE(XModbusPdu, sizeof(XModbusPdu));
    return XVTABLE_DEFAULT;
}
// =============== 数据大小计算器实现 ===============

/**
 * @brief 请求最小数据大小表
 * 根据Modbus协议规范，每个功能码的最小数据大小（不含功能码字节）
 */
static int16_t requestMinimumDataSizeForCode(XModbusPdu_FunctionCode fc) {
    switch (fc) {
    case XModbusPdu_ReadCoils:              return 4;  // 起始地址 + 数量
    case XModbusPdu_ReadDiscreteInputs:     return 4;
    case XModbusPdu_ReadHoldingRegisters:    return 4;
    case XModbusPdu_ReadInputRegisters:     return 4;
    case XModbusPdu_WriteSingleCoil:        return 4;  // 地址 + 值
    case XModbusPdu_WriteSingleRegister:    return 4;
    case XModbusPdu_ReadExceptionStatus:    return 0;
    case XModbusPdu_Diagnostics:            return 2;  // 子功能码
    case XModbusPdu_GetCommEventCounter:    return 0;
    case XModbusPdu_GetCommEventLog:        return 0;
    case XModbusPdu_WriteMultipleCoils:     return 5;  // 地址 + 数量 + 字节数
    case XModbusPdu_WriteMultipleRegisters: return 5;
    case XModbusPdu_ReportServerId:         return 0;
    case XModbusPdu_ReadFileRecord:         return 5;  // 字节数 + 引用类型 + 文件号 + 记录号 + 记录长度
    case XModbusPdu_WriteFileRecord:        return 5;
    case XModbusPdu_MaskWriteRegister:      return 6;  // 地址 + And掩码 + Or掩码
    case XModbusPdu_ReadWriteMultipleRegisters: return 9;  // 读地址 + 读数量 + 写地址 + 写数量 + 字节数
    case XModbusPdu_ReadFifoQueue:          return 2;  // FIFO地址
    case XModbusPdu_EncapsulatedInterfaceTransport: return 2;  // MEI类型 + 数据
    default:                                return -1; // 未知功能码
    }
}

/**
 * @brief 响应最小数据大小表
 */
static int16_t responseMinimumDataSizeForCode(XModbusPdu_FunctionCode fc) {
    switch (fc) {
    case XModbusPdu_ReadCoils:              return 1;  // 字节数
    case XModbusPdu_ReadDiscreteInputs:     return 1;
    case XModbusPdu_ReadHoldingRegisters:    return 1;
    case XModbusPdu_ReadInputRegisters:     return 1;
    case XModbusPdu_WriteSingleCoil:        return 4;  // 回显地址 + 值
    case XModbusPdu_WriteSingleRegister:    return 4;
    case XModbusPdu_ReadExceptionStatus:    return 1;  // 状态字节
    case XModbusPdu_Diagnostics:            return 2;  // 子功能码 + 数据
    case XModbusPdu_GetCommEventCounter:    return 4;  // 状态 + 事件计数
    case XModbusPdu_GetCommEventLog:        return 5;  // 状态 + 事件计数 + 报文计数 + 事件
    case XModbusPdu_WriteMultipleCoils:     return 4;  // 地址 + 数量
    case XModbusPdu_WriteMultipleRegisters: return 4;
    case XModbusPdu_ReportServerId:         return 1;  // 字节数
    case XModbusPdu_ReadFileRecord:         return 3;  // 字节数 + 数据
    case XModbusPdu_WriteFileRecord:        return 3;
    case XModbusPdu_MaskWriteRegister:      return 6;  // 地址 + And掩码 + Or掩码
    case XModbusPdu_ReadWriteMultipleRegisters: return 1;  // 字节数
    case XModbusPdu_ReadFifoQueue:          return 2;  // 字节数 (2字节计数)
    case XModbusPdu_EncapsulatedInterfaceTransport: return 2;  // MEI类型 + 数据
    default:                                return -1;
    }
}

/**
 * @brief 计算请求PDU的实际数据大小
 * 对于固定长度的请求，直接返回最小大小；对于变长请求，解析PDU数据计算
 */
static int16_t calculateRequestDataSize(const XModbusRequest* pdu) {
    if (!pdu) return -1;
    XModbusPdu_FunctionCode fc = XModbusPdu_functionCode((const XModbusPdu*)pdu);

    // 对于固定长度请求，最小大小即为实际大小
    switch (fc) {
    case XModbusPdu_ReadCoils:
    case XModbusPdu_ReadDiscreteInputs:
    case XModbusPdu_ReadHoldingRegisters:
    case XModbusPdu_ReadInputRegisters:
    case XModbusPdu_WriteSingleCoil:
    case XModbusPdu_WriteSingleRegister:
    case XModbusPdu_ReadExceptionStatus:
    case XModbusPdu_Diagnostics:            // 固定2字节子功能码
    case XModbusPdu_GetCommEventCounter:
    case XModbusPdu_GetCommEventLog:
    case XModbusPdu_ReportServerId:
    case XModbusPdu_MaskWriteRegister:
    case XModbusPdu_ReadFifoQueue:
        return requestMinimumDataSizeForCode(fc);
    case XModbusPdu_WriteMultipleCoils: {
        // 数据 = 地址2 + 数量2 + 字节数1 + 实际数据
        int16_t minSize = requestMinimumDataSizeForCode(fc);
        if (minSize < 0) return -1;
        const XByteArray* data = ((const XModbusPdu*)pdu)->m_data;
        if (!data || XByteArray_size_base(data) < minSize) return -1;
        // 第4个字节（索引4）是字节数
        uint8_t byteCount = XByteArray_at_base(data, 4);
        return minSize + byteCount;
    }
    case XModbusPdu_WriteMultipleRegisters: {
        int16_t minSize = requestMinimumDataSizeForCode(fc);
        if (minSize < 0) return -1;
        const XByteArray* data = ((const XModbusPdu*)pdu)->m_data;
        if (!data || XByteArray_size_base(data) < minSize) return -1;
        uint8_t byteCount = XByteArray_at_base(data, 4);
        return minSize + byteCount;
    }
    case XModbusPdu_ReadFileRecord:
    case XModbusPdu_WriteFileRecord: {
        const XByteArray* data = ((const XModbusPdu*)pdu)->m_data;
        if (!data || XByteArray_isEmpty_base(data)) return -1;
        // 第0个字节是字节数（包含自身）
        uint8_t byteCount = XByteArray_at_base(data, 0);
        return (int16_t)(1 + byteCount);  // 1字节计数 + 实际数据
    }
    case XModbusPdu_ReadWriteMultipleRegisters: {
        int16_t minSize = requestMinimumDataSizeForCode(fc);
        if (minSize < 0) return -1;
        const XByteArray* data = ((const XModbusPdu*)pdu)->m_data;
        if (!data || XByteArray_size_base(data) < minSize) return -1;
        // 第8个字节（索引8）是写寄存器字节数
        uint8_t byteCount = XByteArray_at_base(data, 8);
        return minSize + byteCount;
    }
    case XModbusPdu_EncapsulatedInterfaceTransport: {
        // MEI类型1字节 + 变长数据
        int16_t minSize = requestMinimumDataSizeForCode(fc);
        if (minSize < 0) return -1;
        const XByteArray* data = ((const XModbusPdu*)pdu)->m_data;
        if (!data) return -1;
        return (int16_t)XByteArray_size_base(data);
    }
    default:
        return -1;
    }
}

/**
 * @brief 计算响应PDU的实际数据大小
 */
static int16_t calculateResponseDataSize(const XModbusResponse* pdu) {
    if (!pdu) return -1;
    XModbusPdu_FunctionCode fc = XModbusPdu_functionCode((const XModbusPdu*)pdu);

    // 如果异常响应，数据大小为1（异常码）
    if (XModbusPdu_isException((const XModbusPdu*)pdu)) return 1;

    switch (fc) {
    case XModbusPdu_ReadCoils:
    case XModbusPdu_ReadDiscreteInputs:
    case XModbusPdu_ReadHoldingRegisters:
    case XModbusPdu_ReadInputRegisters:
    case XModbusPdu_WriteSingleCoil:
    case XModbusPdu_WriteSingleRegister:
    case XModbusPdu_ReadExceptionStatus:
    case XModbusPdu_WriteMultipleCoils:
    case XModbusPdu_WriteMultipleRegisters:
    case XModbusPdu_MaskWriteRegister:
        return responseMinimumDataSizeForCode(fc);
    case XModbusPdu_Diagnostics: {
        // 子功能码2字节 + 数据
        const XByteArray* data = ((const XModbusPdu*)pdu)->m_data;
        if (!data) return -1;
        return (int16_t)XByteArray_size_base(data);
    }
    case XModbusPdu_GetCommEventCounter:
    case XModbusPdu_GetCommEventLog:
    case XModbusPdu_ReportServerId:
    case XModbusPdu_ReadFileRecord:
    case XModbusPdu_WriteFileRecord: {
        // 变长响应，返回实际数据大小
        const XByteArray* data = ((const XModbusPdu*)pdu)->m_data;
        if (!data) return -1;
        return (int16_t)XByteArray_size_base(data);
    }
    case XModbusPdu_ReadWriteMultipleRegisters: {
        const XByteArray* data = ((const XModbusPdu*)pdu)->m_data;
        if (!data) return -1;
        return (int16_t)XByteArray_size_base(data);
    }
    case XModbusPdu_ReadFifoQueue: {
        const XByteArray* data = ((const XModbusPdu*)pdu)->m_data;
        if (!data || XByteArray_size_base(data) < 2) return -1;
        // 前2字节是字节计数（包含自身）
        uint16_t byteCount = (uint16_t)(XByteArray_at_base(data, 0) << 8 | XByteArray_at_base(data, 1));
        return (int16_t)(2 + byteCount);  // 2字节计数 + 实际数据
    }
    case XModbusPdu_EncapsulatedInterfaceTransport: {
        const XByteArray* data = ((const XModbusPdu*)pdu)->m_data;
        if (!data) return -1;
        return (int16_t)XByteArray_size_base(data);
    }
    default:
        return -1;
    }
}

// =============== 请求计算器注册表 ===============
#define MAX_CALC_REGISTRATIONS 32

typedef struct {
    XModbusPdu_FunctionCode fc;
    XModbusRequest_CalcFuncPtr func;
} RequestCalcEntry;

typedef struct {
    XModbusPdu_FunctionCode fc;
    XModbusResponse_CalcFuncPtr func;
} ResponseCalcEntry;

static RequestCalcEntry s_requestCalcRegistry[MAX_CALC_REGISTRATIONS];
static int s_requestCalcCount = 0;
static ResponseCalcEntry s_responseCalcRegistry[MAX_CALC_REGISTRATIONS];
static int s_responseCalcCount = 0;

// =============== 公共API实现 ===============

int16_t XModbusRequest_minimumDataSize(const XModbusRequest* pdu) {
    if (!pdu) return -1;
    XModbusPdu_FunctionCode fc = XModbusPdu_functionCode((const XModbusPdu*)pdu);
    return requestMinimumDataSizeForCode(fc);
}

int16_t XModbusRequest_calculateDataSize(const XModbusRequest* pdu) {
    if (!pdu) return -1;
    XModbusPdu_FunctionCode fc = XModbusPdu_functionCode((const XModbusPdu*)pdu);

    // 先查询注册表中是否有自定义计算器
    for (int i = 0; i < s_requestCalcCount; i++) {
        if (s_requestCalcRegistry[i].fc == fc) {
            return s_requestCalcRegistry[i].func(pdu);
        }
    }
    // 使用默认实现
    return calculateRequestDataSize(pdu);
}

void XModbusRequest_registerDataSizeCalculator(XModbusPdu_FunctionCode fc, XModbusRequest_CalcFuncPtr func) {
    if (!func) {
        // 传入NULL：移除注册
        for (int i = 0; i < s_requestCalcCount; i++) {
            if (s_requestCalcRegistry[i].fc == fc) {
                s_requestCalcRegistry[i] = s_requestCalcRegistry[--s_requestCalcCount];
                return;
            }
        }
        return;
    }

    // 如果已注册，更新
    for (int i = 0; i < s_requestCalcCount; i++) {
        if (s_requestCalcRegistry[i].fc == fc) {
            s_requestCalcRegistry[i].func = func;
            return;
        }
    }

    // 新增注册
    if (s_requestCalcCount < MAX_CALC_REGISTRATIONS) {
        s_requestCalcRegistry[s_requestCalcCount].fc = fc;
        s_requestCalcRegistry[s_requestCalcCount].func = func;
        s_requestCalcCount++;
    }
}

int16_t XModbusResponse_minimumDataSize(const XModbusResponse* pdu) {
    if (!pdu) return -1;
    XModbusPdu_FunctionCode fc = XModbusPdu_functionCode((const XModbusPdu*)pdu);
    // 异常响应固定1字节
    if (XModbusPdu_isException((const XModbusPdu*)pdu)) return 1;
    return responseMinimumDataSizeForCode(fc);
}

int16_t XModbusResponse_calculateDataSize(const XModbusResponse* pdu) {
    if (!pdu) return -1;
    XModbusPdu_FunctionCode fc = XModbusPdu_functionCode((const XModbusPdu*)pdu);

    // 先查询注册表中是否有自定义计算器
    for (int i = 0; i < s_responseCalcCount; i++) {
        if (s_responseCalcRegistry[i].fc == fc) {
            return s_responseCalcRegistry[i].func(pdu);
        }
    }
    // 使用默认实现
    return calculateResponseDataSize(pdu);
}

void XModbusResponse_registerDataSizeCalculator(XModbusPdu_FunctionCode fc, XModbusResponse_CalcFuncPtr func) {
    if (!func) {
        for (int i = 0; i < s_responseCalcCount; i++) {
            if (s_responseCalcRegistry[i].fc == fc) {
                s_responseCalcRegistry[i] = s_responseCalcRegistry[--s_responseCalcCount];
                return;
            }
        }
        return;
    }

    for (int i = 0; i < s_responseCalcCount; i++) {
        if (s_responseCalcRegistry[i].fc == fc) {
            s_responseCalcRegistry[i].func = func;
            return;
        }
    }

    if (s_responseCalcCount < MAX_CALC_REGISTRATIONS) {
        s_responseCalcRegistry[s_responseCalcCount].fc = fc;
        s_responseCalcRegistry[s_responseCalcCount].func = func;
        s_responseCalcCount++;
    }
}


XVtable* XModbusRequest_class_init(void)
{
    return XModbusPdu_class_init();
}

XVtable* XModbusResponse_class_init(void)
{
    return XModbusPdu_class_init();
}

XVtable* XModbusExceptionResponse_class_init(void)
{
    return XModbusResponse_class_init();
}

// --- 创建与初始化 ---
XModbusPdu* XModbusPdu_create(void) {
    XModbusPdu* pdu = (XModbusPdu*)XMalloc_System(sizeof(XModbusPdu));
    if (pdu) XModbusPdu_init(pdu);
    Set_Class_MemoryFree(pdu, XFree_System);
    return pdu;
}

XModbusPdu* XModbusPdu_create_copy(const XModbusPdu* pdu)
{
    if (!pdu) return NULL;
    XModbusPdu* newPdu = XModbusPdu_create();
    if (newPdu) XModbusPdu_copy_base(newPdu,pdu);
    return newPdu;
}

XModbusPdu* XModbusPdu_create_move(XModbusPdu* pdu)
{
    if (!pdu) return NULL;
    XModbusPdu* newPdu = XModbusPdu_create();
    if (newPdu) XModbusPdu_move_base(newPdu, pdu);
    return newPdu;
}

XModbusPdu* XModbusPdu_create_with_code(XModbusPdu_FunctionCode code) {
    XModbusPdu* pdu = (XModbusPdu*)XMalloc_System(sizeof(XModbusPdu));
    if (pdu) {
        XModbusPdu_init_with_code(pdu, code);
        Set_Class_MemoryFree(pdu, XFree_System);
    }
    return pdu;
}

void XModbusPdu_init(XModbusPdu* pdu) {
    if (!pdu) return;
    XClass_init((XClass*)pdu);
    XClassGetVtable(pdu) = XModbusPdu_class_init();
    pdu->m_code = XModbusPdu_Invalid;
    pdu->m_data = XByteArray_create();
}

void XModbusPdu_init_with_code(XModbusPdu* pdu, XModbusPdu_FunctionCode code) {
    XModbusPdu_init(pdu);
    if (pdu) pdu->m_code = code;
}

XModbusRequest* XModbusRequest_create(void)
{
    XModbusRequest* req = (XModbusRequest*)XMalloc_System(sizeof(XModbusRequest));
    if (req) {
        XModbusRequest_init(req);
        Set_Class_MemoryFree(req, XFree_System);
    }
    return req;
}

XModbusRequest* XModbusRequest_create_copy(const XModbusRequest* req)
{
    if (!req)return NULL;
    XModbusRequest* newReq = XModbusRequest_create();
    if (newReq)XModbusRequest_copy_base(newReq,req);
    return newReq;
}

XModbusRequest* XModbusRequest_create_move(XModbusRequest* req)
{
    if (!req)return NULL;
    XModbusRequest* newReq = XModbusRequest_create();
    if (newReq)XModbusRequest_move_base(newReq, req);
    return newReq;
}

XModbusRequest* XModbusRequest_create_with_code(XModbusPdu_FunctionCode code)
{
    XModbusRequest* req = XModbusRequest_create();
    if (req) XModbusRequest_init_with_code(req, code);
    return req;
}

void XModbusRequest_init(XModbusRequest* req)
{
    if (!req) return;
    XModbusPdu_init(&req->m_base);
    XClassGetVtable(&req->m_base.m_class) = XModbusRequest_class_init();
}

void XModbusRequest_init_with_code(XModbusRequest* req, XModbusPdu_FunctionCode code)
{
    XModbusRequest_init(req);
    if (req) req->m_base.m_code = code;
}

XModbusResponse* XModbusResponse_create(void)
{
    XModbusResponse* resp = (XModbusResponse*)XMalloc_System(sizeof(XModbusResponse));
    if (resp) {
        XModbusResponse_init(resp);
        Set_Class_MemoryFree(resp, XFree_System);
    }
    return resp;
}
XModbusResponse* XModbusResponse_create_copy(XModbusResponse* response)
{
    XModbusResponse* resp = XModbusResponse_create();
    if (resp)XModbusResponse_copy_base(resp, response);
    return resp;
}
XModbusResponse* XModbusResponse_create_move(XModbusResponse* response)
{
    XModbusResponse* resp = XModbusResponse_create();
    if (resp)XModbusResponse_move_base(resp, response);
    return resp;
}
XModbusResponse* XModbusResponse_create_with_code(XModbusPdu_FunctionCode code) {
    XModbusResponse* resp = XModbusResponse_create();
    if (resp) XModbusResponse_init_with_code(resp, code);
    return resp;
}

void XModbusResponse_init(XModbusResponse* resp) {
    if (!resp) return;
    XModbusPdu_init(&resp->m_base);
    XClassGetVtable(&resp->m_base.m_class) = XModbusResponse_class_init();
}

void XModbusResponse_init_with_code(XModbusResponse* resp, XModbusPdu_FunctionCode code) {
    XModbusResponse_init(resp);
    if (resp) resp->m_base.m_code = code;
}
XModbusExceptionResponse* XModbusExceptionResponse_create(void) {
    XModbusExceptionResponse* exc = (XModbusExceptionResponse*)XMalloc_System(sizeof(XModbusExceptionResponse));
    if (exc) {
        XModbusExceptionResponse_init(exc);
        Set_Class_MemoryFree(exc, XFree_System);
    }
    return exc;
}

XModbusExceptionResponse* XModbusExceptionResponse_create_copy(const XModbusExceptionResponse* res)
{
    XModbusExceptionResponse* newExc = XModbusExceptionResponse_create();
    if (newExc)XModbusExceptionResponse_copy_base(newExc,res);
    return newExc;
}

XModbusExceptionResponse* XModbusExceptionResponse_create_move(XModbusExceptionResponse* res)
{
    XModbusExceptionResponse* newExc = XModbusExceptionResponse_create();
    if (newExc)XModbusExceptionResponse_move_base(newExc, res);
    return newExc;
}

XModbusExceptionResponse* XModbusExceptionResponse_create_with_function_and_exception(
    XModbusPdu_FunctionCode functionCode, XModbusPdu_ExceptionCode exceptionCode) {
    XModbusExceptionResponse* exc = XModbusExceptionResponse_create();
    if (exc) {
        XModbusExceptionResponse_init_with_function_and_exception(exc, functionCode, exceptionCode);
    }
    return exc;
}

void XModbusExceptionResponse_init(XModbusExceptionResponse* exc) {
    if (!exc) return;
    XModbusResponse_init(&exc->m_base);
    XClassGetVtable(&exc->m_base.m_base.m_class) = XModbusExceptionResponse_class_init();
}

void XModbusExceptionResponse_init_with_function_and_exception(
    XModbusExceptionResponse* exc, XModbusPdu_FunctionCode functionCode, XModbusPdu_ExceptionCode exceptionCode) {
    XModbusExceptionResponse_init(exc);
    if (exc) {
        // 设置带异常位的功能码
        XModbusPdu_FunctionCode excCode = (XModbusPdu_FunctionCode)(functionCode | XMODBUS_PDU_EXCEPTION_BYTE);
        exc->m_base.m_base.m_code = excCode;
        // 设置异常码为数据
        uint8_t ec = (uint8_t)exceptionCode;
        XByteArray_clear_base(exc->m_base.m_base.m_data);
        XByteArray_push_back_2(exc->m_base.m_base.m_data, &ec, 1);
    }
}
void XModbusExceptionResponse_setExceptionCode(XModbusExceptionResponse* exc, XModbusPdu_ExceptionCode ec) {
    if (!exc) return;
    uint8_t code = (uint8_t)ec;
    XByteArray_clear_base(exc->m_base.m_base.m_data);
    XByteArray_push_back_2(exc->m_base.m_base.m_data, &code, 1);
}
// --- 析构 ---
static void VXModbusPdu_deinit(XModbusPdu* pdu) {
    if (!pdu) return;
    if (pdu->m_data) {
        XByteArray_delete_base(pdu->m_data);
        pdu->m_data = NULL;
    }
}

void VXModbusPdu_copy(XModbusPdu* pdu, XModbusPdu* src)
{
    if (XClassIsVtableNull(pdu))
        XModbusPdu_init(pdu);
    pdu->m_code = src->m_code;
    XByteArray_copy_base(pdu->m_data,src->m_data);
}

void VXModbusPdu_move(XModbusPdu * pdu, XModbusPdu * src)
{
    if (XClassIsVtableNull(pdu))
        XModbusPdu_init(pdu);
    XSwap(pdu,src,sizeof(XModbusPdu));
}

// --- 核心接口实现 ---
bool XModbusPdu_isValid(const XModbusPdu* pdu) {
    if (!pdu) return false;
    bool validCode = (pdu->m_code >= XModbusPdu_ReadCoils && pdu->m_code < XModbusPdu_UndefinedFunctionCode);
    bool validSize = (XByteArray_size_base(pdu->m_data) < 253);
    return validCode && validSize;
}

bool XModbusPdu_isException(const XModbusPdu* pdu) {
    return pdu && (pdu->m_code & XMODBUS_PDU_EXCEPTION_BYTE);
}

XModbusPdu_ExceptionCode XModbusPdu_exceptionCode(const XModbusPdu* pdu) {
    if (!pdu || !XModbusPdu_isException(pdu) || XByteArray_isEmpty_base(pdu->m_data)) {
        return XModbusPdu_ExtendedException;
    }
    return (XModbusPdu_ExceptionCode)XByteArray_at_base(pdu->m_data, 0);
}

int16_t XModbusPdu_size(const XModbusPdu* pdu) {
    return pdu ? (XModbusPdu_dataSize(pdu) + 1) : 0;
}

int16_t XModbusPdu_dataSize(const XModbusPdu* pdu) {
    return pdu ? (int16_t)XByteArray_size_base(pdu->m_data) : 0;
}

XModbusPdu_FunctionCode XModbusPdu_functionCodeRaw(const XModbusPdu* pdu) {
    return pdu ? pdu->m_code : XModbusPdu_Invalid;
}

XModbusPdu_FunctionCode XModbusPdu_functionCode(const XModbusPdu* pdu) {
    return pdu ? (XModbusPdu_FunctionCode)(pdu->m_code & ~XMODBUS_PDU_EXCEPTION_BYTE) : XModbusPdu_Invalid;
}

void XModbusPdu_setFunctionCode(XModbusPdu* pdu, XModbusPdu_FunctionCode code) {
    if (pdu) pdu->m_code = code;
}

XByteArray* XModbusPdu_data(const XModbusPdu* pdu) {
    if (!pdu) return NULL;
    return XByteArray_create_copy(pdu->m_data);
}

void XModbusPdu_setData(XModbusPdu* pdu, const uint8_t* newData, size_t size) {
    if (!pdu || !pdu->m_data) return;
    XByteArray_clear_base(pdu->m_data);
    if (newData && size > 0) {
        XByteArray_push_back_2(pdu->m_data, newData, size);
    }
}
