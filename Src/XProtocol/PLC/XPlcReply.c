#include "XPlc_config.h"
#if XPROTOCOL_ON
#if XPLC_ON
#if XPLC_CORE_ON
#include "XPlcReply.h"
#include "XPrintf.h"
#include "XPlcReply_Protected.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"
#include "XByteArray.h"
#include "XVector.h"

// 虚函数重载
static void VXPlcReply_deinit(XPlcReply* reply);

XVtable* XPlcReply_class_init(void) {
    XVTABLE_INIT_DEFAULT(XPlcReply)

    // 继承XObject
    XVTABLE_INHERIT_XCLASS(XObject);
    // 重载析构函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPlcReply_deinit);

    XCLASS_SHOW_SIZE(XPlcReply, sizeof(XPlcReply));
    return XVTABLE_DEFAULT;
}

XPlcReply* XPlcReply_create_ex(XMemoryType memory, XPlcReply_ReplyType type) {
    XPlcReply* reply = (XPlcReply*)XMemory_malloc(sizeof(XPlcReply), memory);
    if (reply) {
        XPlcReply_init(reply, type);
        Set_Class_Memory(reply, memory); Set_Class_IsHeap(reply, true);
    }
    return reply;
}

void XPlcReply_init(XPlcReply* reply, XPlcReply_ReplyType type) {
    if (!reply) return;

    // 初始化基类
    XObject_init((XObject*)reply);
    XClassGetVtable(reply) = XPlcReply_class_init();

    // 初始化成员
    reply->m_type = (uint8_t)type;
    reply->m_state = XPlcReply_State_No_Started;
    reply->m_finished = false;
    reply->m_error = XPlcDevice_NoError;
    reply->m_errorString = NULL;
    reply->m_pduRef = 0;
    reply->m_result = NULL;
    reply->m_rawResult = NULL;
    reply->m_request = NULL;
    reply->m_intermediateErrors = NULL;
}

static void VXPlcReply_deinit(XPlcReply* reply) {
    if (!reply) return;

    // 释放成员
    if (reply->m_errorString) {
        XString_delete_base((XClass*)reply->m_errorString);
        reply->m_errorString = NULL;
    }

    if (reply->m_result) {
        XVariant_delete_base((XClass*)reply->m_result);
        reply->m_result = NULL;
    }

    if (reply->m_rawResult) {
        XByteArray_delete_base((XClass*)reply->m_rawResult);
        reply->m_rawResult = NULL;
    }

    if (reply->m_request)
    {
        XByteArray_delete_base((XClass*)reply->m_request);
        reply->m_request = NULL;
    }

    if (reply->m_intermediateErrors) {
        XVector_delete_base((XClass*)reply->m_intermediateErrors);
        reply->m_intermediateErrors = NULL;
    }

    // 调用基类析构
    XClass_Deinit_Parent(XObject, (XObject*)reply);
}

// --- Getters ---
XPlcReply_ReplyType XPlcReply_type(const XPlcReply* reply) {
    return reply ? (XPlcReply_ReplyType)reply->m_type : XPlcReply_Raw;
}

XPlcReply_State XPlcReply_state(const XPlcReply* reply)
{
    return reply ? (XPlcReply_State)reply->m_state : XPlcReply_State_No_Started;
}

uint16_t XPlcReply_pduRef(const XPlcReply* reply)
{
    return reply ? reply->m_pduRef : 0;
}

bool XPlcReply_isFinished(const XPlcReply* reply)
{
    return reply && reply->m_finished;
}

XVariant* XPlcReply_result(const XPlcReply* reply) {
    if (!reply || !reply->m_result) return NULL;
    return XVariant_create_copy(reply->m_result);
}

const XVariant* XPlcReply_result_const(const XPlcReply* reply)
{
    if (!reply || !reply->m_result) return NULL;
    return reply->m_result;
}

XByteArray* XPlcReply_rawResult(const XPlcReply* reply) {
    if (!reply || !reply->m_rawResult) return NULL;
    return XByteArray_create_copy(reply->m_rawResult);
}

const XByteArray* XPlcReply_rawResult_const(const XPlcReply* reply)
{
    if (!reply || !reply->m_rawResult) return NULL;
    return reply->m_rawResult;
}

XByteArray* XPlcReply_request(const XPlcReply* reply)
{
    if (!reply || !reply->m_request) return NULL;
    return XByteArray_create_copy(reply->m_request);
}

const XByteArray* XPlcReply_request_const(const XPlcReply* reply)
{
    if (!reply || !reply->m_request) return NULL;
    return reply->m_request;
}

XString* XPlcReply_errorString(const XPlcReply* reply) {
    if (!reply) return NULL;
    if (reply->m_errorString) {
        return XString_create_copy(reply->m_errorString);
    }
    // Fallback to empty string
    return XString_create_fmt_utf8("");
}

XPlcDevice_Error XPlcReply_error(const XPlcReply* reply) {
    return reply ? (XPlcDevice_Error)reply->m_error : XPlcDevice_UnknownError;
}

// --- Setters ---
void XPlcReply_setResult(XPlcReply* reply, const XVariant* value) {
    if (!reply || !value) return;
    if (reply->m_result) {
        XCopy(reply->m_result, value);
    }
    else
    {
        reply->m_result = XVariant_create_copy(value);
    }
}

void XPlcReply_setResult_move(XPlcReply* reply, XVariant* value)
{
    if (!reply || !value) return;
    if (reply->m_result) {
        XMove(reply->m_result, value);
    }
    else
    {
        reply->m_result = XVariant_create_move(value);
    }
}

void XPlcReply_setResult_ref(XPlcReply* reply, XVariant* value)
{
    if (!reply) return;
    if (reply->m_result) {
        XVariant_delete_base((XClass*)reply->m_result);
        reply->m_result = NULL;
    }
    if (value) {
        reply->m_result = value;
    }
}

void XPlcReply_setRawResult(XPlcReply* reply, const XByteArray* data) {
    if (!reply || !data) return;
    if (reply->m_rawResult)
        XCopy(reply->m_rawResult, data);
    else
        reply->m_rawResult = XByteArray_create_copy(data);
}

void XPlcReply_setRawResult_move(XPlcReply* reply, XByteArray* data)
{
    if (!reply || !data) return;
    if (reply->m_rawResult)
        XMove(reply->m_rawResult, data);
    else
        reply->m_rawResult = XByteArray_create_move(data);
}

void XPlcReply_setRawResult_ref(XPlcReply* reply, XByteArray* data)
{
    if (!reply) return;
    if (reply->m_rawResult) {
        XByteArray_delete_base((XClass*)reply->m_rawResult);
        reply->m_rawResult = NULL;
    }
    if (data) {
        reply->m_rawResult = data;
    }
}

void XPlcReply_setState(XPlcReply* reply, XPlcReply_State state)
{
    if (!reply || (XPlcReply_State)reply->m_state == state) return;
    bool wasFinished = reply->m_finished;
    reply->m_state = (uint8_t)state;
    if (state == XPlcReply_State_Finished || state == XPlcReply_State_Timeout)
        reply->m_finished = true;
    else if (state != XPlcReply_State_No_Started)
        reply->m_finished = false;
    XPlcReply_stateChanged_signal(reply, state);
    if (reply->m_finished && !wasFinished)
        XPlcReply_finished_signal(reply);
}

void XPlcReply_setPduRef(XPlcReply* reply, uint16_t pduRef)
{
    if (!reply) return;
    reply->m_pduRef = pduRef;
}

void XPlcReply_setFinished(XPlcReply* reply, bool isFinished) {
    if (!reply || reply->m_finished == isFinished) return;

    reply->m_finished = isFinished;
    if (isFinished)
        XPlcReply_finished_signal(reply);
}

void XPlcReply_setError(XPlcReply* reply, XPlcDevice_Error error, const char* errorText) {
    if (!reply) return;
    reply->m_error = (uint8_t)error;

    // 内部重试路径会经 NoError 复位；错误路径在下方完成回复
    if (error == XPlcDevice_NoError) {
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

    XPlcReply_errorOccurred_signal(reply, error);
    XPlcReply_setFinished(reply, true);
}

// --- Intermediate Errors ---
XVector* XPlcReply_intermediateErrors(const XPlcReply* reply) {
    if (!reply || !reply->m_intermediateErrors) return NULL;
    return XVector_create_copy(reply->m_intermediateErrors);
}

void XPlcReply_addIntermediateError(XPlcReply* reply, XPlcDevice_IntermediateError error) {
    if (!reply) return;
    if (!reply->m_intermediateErrors) reply->m_intermediateErrors = XVector_create(sizeof(XPlcDevice_IntermediateError));
    XVector_append_1_base(reply->m_intermediateErrors, &error);
    // 发射信号
    XPlcReply_intermediateErrorOccurred_signal(reply, error);
}

void XPlcReply_clearIntermediateError(XPlcReply* reply)
{
    if (!reply || !reply->m_intermediateErrors) return;
    XVector_clear_base((XContainer*)reply->m_intermediateErrors);
}

// --- Signals ---
// 信号 id 说明：XClass 框架默认以“信号函数自身地址”作信号标识（XEmitSignal 返回
// (size_t)(signal)，连接侧 XSignal 宏经调用信号函数取回同一地址）。Release 链接期
// /OPT:ICF 会把重定位后逐字节同形的 COMDAT 函数折叠为同一地址：本类
// errorOccurred/stateChanged/intermediateErrorOccurred 三个包装函数仅参数的枚举
// 类型名不同（均发一个 4 字节枚举 varlist），实测 Release 下三者同址，连接与发射
// 随之串线（Debug 无 ICF 故不现形）。故改用显式编译期常量 id（"XPLR" 前缀 + 信号
// 序号），连接与发射同源，与代码折叠正交；四常量互异且远离镜像地址区间。
#define XPLCREPLY_SIGID_FINISHED        ((size_t)0x58504C52F1A90001ull) /**< finished 信号 id */
#define XPLCREPLY_SIGID_STATECHANGED    ((size_t)0x58504C52F1A90002ull) /**< stateChanged 信号 id */
#define XPLCREPLY_SIGID_ERROROCCURRED   ((size_t)0x58504C52F1A90003ull) /**< errorOccurred 信号 id */
#define XPLCREPLY_SIGID_INTERMEDIATE    ((size_t)0x58504C52F1A90004ull) /**< intermediateErrorOccurred 信号 id */

void* XPlcReply_finished_signal(XPlcReply* reply) {
    if (reply && ((XObject*)reply)->m_signalSlot)
        XObject_emitSignal((XObject*)reply, XPLCREPLY_SIGID_FINISHED, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)XPLCREPLY_SIGID_FINISHED;
}

void* XPlcReply_stateChanged_signal(XPlcReply* reply, XPlcReply_State state)
{
    if (reply && ((XObject*)reply)->m_signalSlot)
        XObject_emitSignal((XObject*)reply, XPLCREPLY_SIGID_STATECHANGED,
            XVarList_Create(XVar(XPlcReply_State, state)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)XPLCREPLY_SIGID_STATECHANGED;
}

void* XPlcReply_errorOccurred_signal(XPlcReply* reply, XPlcDevice_Error error) {
    if (reply && ((XObject*)reply)->m_signalSlot)
        XObject_emitSignal((XObject*)reply, XPLCREPLY_SIGID_ERROROCCURRED,
            XVarList_Create(XVar(XPlcDevice_Error, error)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)XPLCREPLY_SIGID_ERROROCCURRED;
}

void* XPlcReply_intermediateErrorOccurred_signal(XPlcReply* reply, XPlcDevice_IntermediateError error) {
    if (reply && ((XObject*)reply)->m_signalSlot)
        XObject_emitSignal((XObject*)reply, XPLCREPLY_SIGID_INTERMEDIATE,
            XVarList_Create(XVar(XPlcDevice_IntermediateError, error)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)XPLCREPLY_SIGID_INTERMEDIATE;
}

#endif /* XPLC_CORE_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */
