#include "XModbusPdu.h"
#include "XMemory.h"
#include "XByteArray.h"
#include <string.h>

// 虚函数重载
static void VXModbusPdu_deinit(XModbusPdu* pdu);

XVtable* XModbusPdu_class_init(void) {
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

// --- 创建与初始化 ---
XModbusPdu* XModbusPdu_create(void) {
    XModbusPdu* pdu = (XModbusPdu*)XMemory_malloc(sizeof(XModbusPdu));
    if (pdu) XModbusPdu_init(pdu);
    return pdu;
}

XModbusPdu* XModbusPdu_create_copy(XModbusPdu* pdu)
{
    XModbusPdu* new = XModbusPdu_create();
    if (!new)return new;
    new->m_code = pdu->m_code;
    XByteArray_copy_base(new->m_data,pdu->m_data);
    return new;
}

XModbusPdu* XModbusPdu_create_with_code(XModbusPdu_FunctionCode code) {
    XModbusPdu* pdu = (XModbusPdu*)XMemory_malloc(sizeof(XModbusPdu));
    if (pdu) XModbusPdu_init_with_code(pdu, code);
    return pdu;
}

XModbusPdu* XModbusPdu_create_with_code_and_data(XModbusPdu_FunctionCode code, const uint8_t* data, size_t dataSize) {
    XModbusPdu* pdu = (XModbusPdu*)XMemory_malloc(sizeof(XModbusPdu));
    if (pdu) XModbusPdu_init_with_code_and_data(pdu, code, data, dataSize);
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

// --- 异常响应特殊处理 ---
XModbusPdu* XModbusExceptionResponse_create(XModbusPdu_FunctionCode functionCode, XModbusPdu_ExceptionCode exceptionCode) {
    XModbusPdu* pdu = XModbusPdu_create();
    if (pdu) {
        XModbusExceptionResponse_init(pdu, functionCode, exceptionCode);
    }
    return pdu;
}

void XModbusExceptionResponse_init(XModbusPdu* pdu, XModbusPdu_FunctionCode functionCode, XModbusPdu_ExceptionCode exceptionCode) {
    if (!pdu) return;
    XModbusPdu_init(pdu);
    // Set function code with exception bit
    XModbusPdu_FunctionCode excCode = (XModbusPdu_FunctionCode)(functionCode | XMODBUS_PDU_EXCEPTION_BYTE);
    XModbusPdu_setFunctionCode(pdu, excCode);
    // Set exception code as data
    uint8_t ec = (uint8_t)exceptionCode;
    XModbusPdu_setData(pdu, &ec, 1);
}

void XModbusExceptionResponse_setExceptionCode(XModbusPdu* pdu, XModbusPdu_ExceptionCode ec) {
    if (!pdu) return;
    uint8_t code = (uint8_t)ec;
    XModbusPdu_setData(pdu, &code, 1);
}