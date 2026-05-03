#include "XModbusReply.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"

// 虚函数重载
static void VXModbusReply_deinit(XModbusReply* reply);

XVtable* XModbusReply_class_init(void) {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XObject))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif

        // 继承XObject
        XVTABLE_INHERIT_XCLASS(XObject);

    // 重载析构函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusReply_deinit);

#if SHOWCONTAINERSIZE
    printf("XModbusReply size: %zu\n", sizeof(XModbusReply));
#endif
    return XVTABLE_DEFAULT;
}

XModbusReply* XModbusReply_create(XModbusReply_ReplyType type, int serverAddress) {
    XModbusReply* reply = (XModbusReply*)XMemory_malloc(sizeof(XModbusReply));
    if (reply) {
        XModbusReply_init(reply, type, serverAddress);
        Set_Class_MemoryFree(reply, XFree);
    }
    return reply;
}

void XModbusReply_init(XModbusReply* reply, XModbusReply_ReplyType type, int serverAddress) {
    if (!reply) return;

    // 初始化基类
    XObject_init((XObject*)reply);
    XClassGetVtable(reply) = XModbusReply_class_init();

    // 初始化成员
    reply->m_type = type;
    reply->m_serverAddress = serverAddress;
    reply->m_isFinished = false;
    reply->m_error = XModbusDevice_NoError;
    reply->m_errorString = NULL;
    reply->m_result = NULL;
    reply->m_rawResult = NULL;
    reply->m_intermediateErrors = XVector_create(sizeof(XModbusDevice_IntermediateError));
}

static void VXModbusReply_deinit(XModbusReply* reply) {
    if (!reply) return;

    // 释放成员
    if (reply->m_errorString) {
        XString_delete_base(reply->m_errorString);
        reply->m_errorString = NULL;
    }

    if (reply->m_result) {
        XModbusDataUnit_delete_base(reply->m_result);
        reply->m_result = NULL;
    }

    if (reply->m_rawResult) {
        XModbusPdu_delete_base((XModbusPdu*)reply->m_rawResult);
        reply->m_rawResult = NULL;
    }

    if (reply->m_intermediateErrors) {
        XVector_delete_base(reply->m_intermediateErrors);
        reply->m_intermediateErrors = NULL;
    }

    // 调用基类析构
    XObject_deinitLater((XObject*)reply);
}

// --- Getters ---
XModbusReply_ReplyType XModbusReply_type(const XModbusReply* reply) {
    return reply ? reply->m_type : XModbusReply_Raw;
}

int XModbusReply_serverAddress(const XModbusReply* reply) {
    return reply ? reply->m_serverAddress : -1;
}

bool XModbusReply_isFinished(const XModbusReply* reply) {
    return reply && reply->m_isFinished;
}

XModbusDataUnit* XModbusReply_result(const XModbusReply* reply) {
    if (!reply || !reply->m_result) return NULL;
    return (XModbusDataUnit*)XModbusDataUnit_create_copy(reply->m_result);
}

XModbusResponse* XModbusReply_rawResult(const XModbusReply* reply) {
    if (!reply || !reply->m_rawResult) return NULL;
    return (XModbusResponse*)XModbusPdu_create_copy((XModbusPdu*)reply->m_rawResult);
}

XString* XModbusReply_errorString(const XModbusReply* reply) {
    if (!reply) return NULL;
    if (reply->m_errorString) {
        return XString_create_copy(reply->m_errorString);
    }
    // Fallback to empty string
    return XString_create_fmt_utf8("");
}

XModbusDevice_Error XModbusReply_error(const XModbusReply* reply) {
    return reply ? reply->m_error : XModbusDevice_UnknownError;
}

// --- Setters ---
void XModbusReply_setResult(XModbusReply* reply, const XModbusDataUnit* unit) {
    if (!reply) return;
    if (reply->m_result) {
        XModbusDataUnit_delete_base(reply->m_result);
        reply->m_result = NULL;
    }
    if (unit) {
        reply->m_result = (XModbusDataUnit*)XModbusDataUnit_create_copy(unit);
    }
}

void XModbusReply_setRawResult(XModbusReply* reply, const XModbusResponse* response) {
    if (!reply) return;
    if (reply->m_rawResult) {
        XModbusPdu_delete_base((XModbusPdu*)reply->m_rawResult);
        reply->m_rawResult = NULL;
    }
    if (response) {
        reply->m_rawResult = (XModbusResponse*)XModbusPdu_create_copy((XModbusPdu*)response);
    }
}

void XModbusReply_setFinished(XModbusReply* reply, bool finished) {
    if (reply) {
        reply->m_isFinished = finished;
    }
}

void XModbusReply_setError(XModbusReply* reply, XModbusDevice_Error error, const char* errorText) {
    if (!reply) return;
    reply->m_error = error;
    if (reply->m_errorString) {
        XString_delete_base(reply->m_errorString);
        reply->m_errorString = NULL;
    }
    if (errorText) {
        reply->m_errorString = XString_create_fmt_utf8("%s", errorText);
    }
}

// --- Intermediate Errors ---
XVector* XModbusReply_intermediateErrors(const XModbusReply* reply) {
    if (!reply || !reply->m_intermediateErrors) return NULL;
    return XVector_create_copy(reply->m_intermediateErrors);
}

void XModbusReply_addIntermediateError(XModbusReply* reply, XModbusDevice_IntermediateError error) {
    if (!reply || !reply->m_intermediateErrors) return;
    XVector_append_base(reply->m_intermediateErrors, &error);
    // 发射信号
    XModbusReply_intermediateErrorOccurred_signal(reply, error);
}

// --- Signals ---
void* XModbusReply_finished_signal(XModbusReply* reply) {
    XEmitSignal(reply, XModbusReply_finished_signal, NULL, NULL, NULL, XEVENT_PRIORITY_LOWEST);
}

void* XModbusReply_errorOccurred_signal(XModbusReply* reply, XModbusDevice_Error error) {
    XEmitSignal(reply, XModbusReply_errorOccurred_signal,
        XVariant_create_int((int)error), XVariant_delete_base,
        NULL, XEVENT_PRIORITY_LOWEST);
}

void* XModbusReply_intermediateErrorOccurred_signal(XModbusReply* reply, XModbusDevice_IntermediateError error) {
    XEmitSignal(reply, XModbusReply_intermediateErrorOccurred_signal,
        XVariant_create_int((int)error), XVariant_delete_base,
        NULL, XEVENT_PRIORITY_LOWEST);
}