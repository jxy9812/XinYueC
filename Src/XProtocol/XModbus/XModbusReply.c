#include "XModbus_config.h"
#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_CLIENT_ON
#include "XModbusReply.h"
#include "XModbusReply_Protected.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"

// 虚函数重载
static void VXModbusReply_deinit(XModbusReply* reply);

XVtable* XModbusReply_class_init(void) {
    XVTABLE_INIT_DEFAULT(XModbusReply)

    // 继承XObject
    XVTABLE_INHERIT_XCLASS(XObject);
    // 重载析构函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusReply_deinit);

	XCLASS_SHOW_SIZE(XModbusReply, sizeof(XModbusReply));
    return XVTABLE_DEFAULT;
}

XModbusReply* XModbusReply_create(XModbusReply_ReplyType type, int serverAddress) {
    XModbusReply* reply = (XModbusReply*)XMalloc_System(sizeof(XModbusReply));
    if (reply) {
        XModbusReply_init(reply, type, serverAddress);
        Set_Class_MemoryFree(reply, XFree_System);
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
    reply->m_state = XModbusReply_State_No_Started;
    reply->m_finished = false;
    reply->m_error = XModbusDevice_NoError;
    reply->m_errorString = NULL;
    reply->m_result = NULL;
    reply->m_rawResult = NULL;
    reply->m_intermediateErrors = NULL;
    reply->m_request = NULL;
}

static void VXModbusReply_deinit(XModbusReply* reply) {
    if (!reply) return;
    //XPrintf("释放:%p\n",reply);
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

    if (reply->m_request)
    {
        XModbusRequest_delete_base(reply->m_request);
        reply->m_request = NULL;
    }

    if (reply->m_intermediateErrors) {
        XVector_delete_base(reply->m_intermediateErrors);
        reply->m_intermediateErrors = NULL;
    }
        // 调用基类析构
    XClass_Deinit_Parent(XObject, reply);
}

// --- Getters ---
XModbusReply_ReplyType XModbusReply_type(const XModbusReply* reply) {
    return reply ? reply->m_type : XModbusReply_Raw;
}

XModbusReply_State XModbusReply_state(const XModbusReply* reply)
{
    return reply ? reply->m_state : XModbusReply_State_No_Started;
}

int XModbusReply_serverAddress(const XModbusReply* reply) {
    return reply ? reply->m_serverAddress : -1;
}

bool XModbusReply_isFinished(const XModbusReply* reply) 
{
    return reply && reply->m_finished;
}

XModbusDataUnit* XModbusReply_result(const XModbusReply* reply) {
    if (!reply || !reply->m_result) return NULL;
    return (XModbusDataUnit*)XModbusDataUnit_create_copy(reply->m_result);
}

const XModbusDataUnit* XModbusReply_result_const(const XModbusReply* reply)
{
    if (!reply || !reply->m_result) return NULL;
    return reply->m_result;
}

XModbusResponse* XModbusReply_rawResult(const XModbusReply* reply) {
    if (!reply || !reply->m_rawResult) return NULL;
    return (XModbusResponse*)XModbusPdu_create_copy((XModbusPdu*)reply->m_rawResult);
}

const XModbusResponse* XModbusReply_rawResult_const(const XModbusReply* reply)
{
    if (!reply || !reply->m_rawResult) return NULL;
    return reply->m_rawResult;
}

XModbusRequest* XModbusReply_request(const XModbusReply* reply)
{
    return XModbusRequest_create_copy(XModbusReply_request_const(reply));
}

const XModbusRequest* XModbusReply_request_const(const XModbusReply* reply)
{
    if (!reply || !reply->m_request) return NULL;
    return reply->m_request;
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
        XModbusDataUnit_copy_base(reply->m_result, unit);
    }
    else
    {
        reply->m_result = XModbusDataUnit_create_copy(unit);
    }
}

void XModbusReply_setResult_move(XModbusReply* reply, const XModbusDataUnit* unit)
{
    if (!reply) return;
    if (reply->m_result) {
        XModbusDataUnit_move_base(reply->m_result, unit);
    }
    else
    {
        reply->m_result = XModbusDataUnit_create_move(unit);
    }
}

void XModbusReply_setResult_ref(XModbusReply * reply, const XModbusDataUnit * unit)
{
    if (!reply) return;
    if (reply->m_result) {
        XModbusDataUnit_delete_base(reply->m_result);
        reply->m_result = NULL;
    }
    if (unit) {
        reply->m_result = unit;
    }
}

void XModbusReply_setRawResult(XModbusReply* reply, const XModbusResponse* response) {
    if (!reply) return;
    if (reply->m_rawResult) 
        XModbusResponse_copy_base(reply->m_rawResult, response);
    else
        reply->m_rawResult = (XModbusResponse*)XModbusResponse_create_copy((XModbusPdu*)response);
}

void XModbusReply_setRawResult_move(XModbusReply* reply, const XModbusResponse* response)
{
    if (!reply) return;
    if (reply->m_rawResult)
        XModbusResponse_move_base(reply->m_rawResult, response);
    else
        reply->m_rawResult = (XModbusResponse*)XModbusResponse_create_move((XModbusPdu*)response);
}

void XModbusReply_setRawResult_ref(XModbusReply * reply, const XModbusResponse * response)
{
    if (!reply) return;
    if (reply->m_rawResult) {
        XModbusPdu_delete_base((XModbusPdu*)reply->m_rawResult);
        reply->m_rawResult = NULL;
    }
    if (response) {
        reply->m_rawResult = response;
    }
}

void XModbusReply_setState(XModbusReply* reply, XModbusReply_State state)
{
    if (!reply|| reply->m_state == state)return;
    bool wasFinished = reply->m_finished;
    reply->m_state = state;
    if (state == XModbusReply_State_Finished || state == XModbusReply_State_Timeout)
        reply->m_finished = true;
    else if (state != XModbusReply_State_No_Started)
        reply->m_finished = false;
    XModbusReply_stateChanged_signal(reply, state);
    if (reply->m_finished && !wasFinished)
        XModbusReply_finished_signal(reply);
}

void XModbusReply_setFinished(XModbusReply* reply, bool isFinished)
{
    if (!reply || reply->m_finished == isFinished) return;

    reply->m_finished = isFinished;
    if (isFinished)
        XModbusReply_finished_signal(reply);
}

void XModbusReply_setError(XModbusReply* reply, XModbusDevice_Error error, const char* errorText) {
    if (!reply) return;
    reply->m_error = error;

    // Internal retry paths reset a reply through NoError. Qt callers only use
    // this setter for an actual error, which always completes the reply below.
    if (error == XModbusDevice_NoError) {
        if (reply->m_errorString)
            XString_assign_fmt_utf8(reply->m_errorString, "%s", "");
        return;
    }

    if (errorText) {
        if (reply->m_errorString)
            XString_assign_fmt_utf8(reply->m_errorString, "%s", errorText);
        else
            reply->m_errorString = XString_create_fmt_utf8("%s", errorText);
    } else if (reply->m_errorString) {
        XString_assign_fmt_utf8(reply->m_errorString, "%s", "");
    }

    XModbusReply_errorOccurred_signal(reply, error);
    XModbusReply_setFinished(reply, true);
}

// --- Intermediate Errors ---
XVector* XModbusReply_intermediateErrors(const XModbusReply* reply) {
    if (!reply || !reply->m_intermediateErrors) return NULL;
    return XVector_create_copy(reply->m_intermediateErrors);
}

void XModbusReply_addIntermediateError(XModbusReply* reply, XModbusDevice_IntermediateError error) {
    if (!reply ) return;
    if (!reply->m_intermediateErrors)reply->m_intermediateErrors=XVector_create(sizeof(XModbusDevice_IntermediateError));
    XVector_append_1_base(reply->m_intermediateErrors, &error);
    // 发射信号
    XModbusReply_intermediateErrorOccurred_signal(reply, error);
}

void XModbusReply_clearIntermediateError(XModbusReply* reply)
{
    if (!reply|| !reply->m_intermediateErrors) return;
    XVector_clear_base(reply->m_intermediateErrors);
}

// --- Signals ---
void* XModbusReply_finished_signal(XModbusReply* reply) {
    XEmitSignal(reply, XModbusReply_finished_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusReply_stateChanged_signal(XModbusReply* reply, XModbusReply_State state)
{
    XEmitSignal(reply, XModbusReply_stateChanged_signal, XVarList_Create(XVar(XModbusReply_State, state)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusReply_errorOccurred_signal(XModbusReply* reply, XModbusDevice_Error error) {
    XEmitSignal(reply, XModbusReply_errorOccurred_signal, XVarList_Create(XVar(XModbusDevice_Error, error)),NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusReply_intermediateErrorOccurred_signal(XModbusReply* reply, XModbusDevice_IntermediateError error) {
    XEmitSignal(reply, XModbusReply_intermediateErrorOccurred_signal,
        XVarList_Create(XVar(XModbusDevice_IntermediateError, error)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XMODBUS_CLIENT_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */
