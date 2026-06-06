#include "XModbusPdu.h"
#include "XMemory.h"
#include "XByteArray.h"
#include <string.h>

// 虚函数重载
static void VXModbusPdu_deinit(XModbusPdu* pdu);

XVtable* XModbusPdu_class_init(void) 
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XClass))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif

        // 重载析构函数
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusPdu_deinit);

#if SHOWCONTAINERSIZE
    printf("XModbusPdu size: %zu\n", sizeof(XModbusPdu));
#endif
    return XVTABLE_DEFAULT;
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
    if (!newPdu) return NULL;
    newPdu->m_code = pdu->m_code;
    if (pdu->m_data) {
        XByteArray_copy_base(newPdu->m_data, pdu->m_data);
    }
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

XModbusPdu* XModbusPdu_create_with_code_and_data(XModbusPdu_FunctionCode code, const uint8_t* data, size_t dataSize) {
    XModbusPdu* pdu = (XModbusPdu*)XMalloc_System(sizeof(XModbusPdu));
    if (pdu) {
        XModbusPdu_init_with_code_and_data(pdu, code, data, dataSize);
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

void XModbusPdu_init_with_code_and_data(XModbusPdu* pdu, XModbusPdu_FunctionCode code, const uint8_t* data, size_t dataSize) {
    XModbusPdu_init_with_code(pdu, code);
    if (pdu && data && dataSize > 0) {
        XByteArray_append_array_base(pdu->m_data, data, dataSize);
    }
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

XModbusRequest* XModbusRequest_create_with_code(XModbusPdu_FunctionCode code)
{
    XModbusRequest* req = XModbusRequest_create();
    if (req) XModbusRequest_init_with_code(req, code);
    return req;
}

XModbusRequest* XModbusRequest_create_with_code_and_data(XModbusPdu_FunctionCode code, const uint8_t* data, size_t size)
{
    XModbusRequest* req = XModbusRequest_create();
    if (req) XModbusRequest_init_with_code_and_data(req, code, data, size);
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

void XModbusRequest_init_with_code_and_data(XModbusRequest* req, XModbusPdu_FunctionCode code, const uint8_t* data, size_t size)
{
    XModbusRequest_init_with_code(req, code);
    if (req && data && size > 0) {
        XByteArray_append_array_base(req->m_base.m_data, data, size);
    }
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
XModbusResponse* XModbusResponse_create_with_code(XModbusPdu_FunctionCode code) {
    XModbusResponse* resp = XModbusResponse_create();
    if (resp) XModbusResponse_init_with_code(resp, code);
    return resp;
}

XModbusResponse* XModbusResponse_create_with_code_and_data(XModbusPdu_FunctionCode code, const uint8_t* data, size_t size) {
    XModbusResponse* resp = XModbusResponse_create();
    if (resp) XModbusResponse_init_with_code_and_data(resp, code, data, size);
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

void XModbusResponse_init_with_code_and_data(XModbusResponse* resp, XModbusPdu_FunctionCode code, const uint8_t* data, size_t size) {
    XModbusResponse_init_with_code(resp, code);
    if (resp && data && size > 0) {
        XByteArray_append_array_base(resp->m_base.m_data, data, size);
    }
}
XModbusExceptionResponse* XModbusExceptionResponse_create(void) {
    XModbusExceptionResponse* exc = (XModbusExceptionResponse*)XMalloc_System(sizeof(XModbusExceptionResponse));
    if (exc) {
        XModbusExceptionResponse_init(exc);
        Set_Class_MemoryFree(exc, XFree_System);
    }
    return exc;
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
        XByteArray_append_array_base(exc->m_base.m_base.m_data, &ec, 1);
    }
}
void XModbusExceptionResponse_setExceptionCode(XModbusExceptionResponse* exc, XModbusPdu_ExceptionCode ec) {
    if (!exc) return;
    uint8_t code = (uint8_t)ec;
    XByteArray_clear_base(exc->m_base.m_base.m_data);
    XByteArray_append_array_base(exc->m_base.m_base.m_data, &code, 1);
}
// --- 析构 ---
static void VXModbusPdu_deinit(XModbusPdu* pdu) {
    if (!pdu) return;
    if (pdu->m_data) {
        XByteArray_delete_base(pdu->m_data);
        pdu->m_data = NULL;
    }
    XClass_deinit_base((XClass*)pdu);
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
    return (XModbusPdu_ExceptionCode)XByteArray_At_Base(pdu->m_data, 0);
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
        XByteArray_append_array_base(pdu->m_data, newData, size);
    }
}
