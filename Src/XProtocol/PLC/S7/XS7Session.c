#include "XPlc_config.h"
#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON
#include "XS7Session.h"
#include "XS7Tpkt.h"
#include "XS7Cotp.h"
#include "XS7Pdu.h"
#include "XMemory.h"
#include <string.h>

/* ==========================================================================
 * S7 会话状态机实现
 *
 * 事件传递采用"缓冲即队列 + 单事件槽"模式：
 * - feed 只把原始字节追加进 m_buffer；
 * - parseMore 仅在事件槽为空时从 m_buffer 头部解析一帧并填槽，
 *   因此事件顺序与字节流顺序严格一致；
 * - takeEvent 取走事件后再触发 parseMore 补槽——此时上一个 S7Frame
 *   的借用数据（m_frame）已按契约被调用方读完，可以安全覆盖。
 * 坏包处理：头部可解析（ver=3 且 total 合法）时按 TPKT len 精确推进；
 * 头部不可解析时逐字节重同步，禁止整段清空缓冲。
 * ========================================================================== */

// =============== 虚函数前置声明 ===============
static void VX7Session_deinit(XS7Session* session);

// =============== 内部辅助 ===============

/** @brief 从 m_buffer 头部解析一帧并填充事件槽（仅槽空时解析） */
static void xs7SessionParseMore(XS7Session* session)
{
    if (!session || !session->m_buffer) return;

    while (session->m_pendingEvent == XS7SessionEv_None) {
        size_t bufLen = XByteArray_size_base(session->m_buffer);
        if (bufLen == 0) return;

        const uint8_t* data = XByteArray_data(session->m_buffer);
        if (!data) return;

        size_t total = 0;
        if (!XS7Tpkt_peekLength(data, bufLen, &total)) {
            if (bufLen < 4) {
                return;                     /* 半包：等更多字节 */
            }
            /* 坏头：头部不可解析，逐字节重同步并记录 */
            XERROR_PRINTF("[XS7Session] bad TPKT header, resync 1 byte (0x%02X)\n", data[0]);
            XByteArray_remove_base(session->m_buffer, 0, 1);
            continue;                       /* 精确推进，不清空缓冲 */
        }

        if (bufLen < total) {
            return;                         /* 整帧未到齐 */
        }

        /* 已有一整帧：[0, total) */
        switch (session->m_state) {
        case XS7Session_CrSent: {
            /* 期望 CC（连接确认） */
            XS7CotpCcInfo info;
            memset(&info, 0, sizeof(info));
            if (XS7Cotp_parseCc(data, total, &info)) {
                /* CC 接受：状态保持 CrSent，等客户端调 buildNextHandshake 发 Setup */
                XByteArray_remove_base(session->m_buffer, 0, (int64_t)total);
                session->m_pendingEvent = XS7SessionEv_CcAccepted;
                return;
            }
            XERROR_PRINTF("[XS7Session] COTP CC rejected\n");
            XByteArray_remove_base(session->m_buffer, 0, (int64_t)total);
            session->m_state = XS7Session_Error;
            session->m_pendingEvent = XS7SessionEv_Error;
            return;
        }

        case XS7Session_SetupSent: {
            /* 期望 SetupCommunication 应答（DT 帧，先剥 TPKT/COTP） */
            size_t s7Off = 0, s7Len = 0;
            if (!XS7Cotp_extractDt(data, total, &s7Off, &s7Len)) {
                XERROR_PRINTF("[XS7Session] setup ack frame: bad DT header\n");
                XByteArray_remove_base(session->m_buffer, 0, (int64_t)total);
                session->m_state = XS7Session_Error;
                session->m_pendingEvent = XS7SessionEv_Error;
                return;
            }
            uint16_t negotiated = 0, amq = 0;
            if (XS7Pdu_parseSetupAck(data + s7Off, s7Len, &negotiated, &amq)) {
                session->m_negotiatedPduLen = negotiated;
                XByteArray_remove_base(session->m_buffer, 0, (int64_t)total);
                session->m_state = XS7Session_Ready;
                session->m_pendingEvent = XS7SessionEv_HandshakeDone;
                return;
            }
            XERROR_PRINTF("[XS7Session] SetupCommunication ack parse failed\n");
            XByteArray_remove_base(session->m_buffer, 0, (int64_t)total);
            session->m_state = XS7Session_Error;
            session->m_pendingEvent = XS7SessionEv_Error;
            return;
        }

        case XS7Session_Ready: {
            /* 业务 PDU（DT 帧） */
            size_t s7Off = 0, s7Len = 0;
            if (!XS7Cotp_extractDt(data, total, &s7Off, &s7Len)) {
                XERROR_PRINTF("[XS7Session] ready frame: bad DT header\n");
                XByteArray_remove_base(session->m_buffer, 0, (int64_t)total);
                session->m_state = XS7Session_Error;
                session->m_pendingEvent = XS7SessionEv_Error;
                return;
            }
            /* 拷出 S7 PDU 到 m_frame（缓冲随后被移除，必须拷贝） */
            XByteArray_clear_base(session->m_frame);
            XByteArray_append_2(session->m_frame, data + s7Off, s7Len);
            XByteArray_remove_base(session->m_buffer, 0, (int64_t)total);
            session->m_pendingEvent = XS7SessionEv_S7Frame;
            return;
        }

        case XS7Session_Idle:
        default: {
            /* 未开始握手就收到数据：协议错误 */
            XERROR_PRINTF("[XS7Session] unexpected frame in state %d\n", (int)session->m_state);
            XByteArray_remove_base(session->m_buffer, 0, (int64_t)total);
            session->m_state = XS7Session_Error;
            session->m_pendingEvent = XS7SessionEv_Error;
            return;
        }
        }
    }
}

// =============== 类初始化 ===============
XVtable* XS7Session_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XS7Session)
    /* 继承 XObject（无新增虚槽） */
    XVTABLE_INHERIT_XCLASS(XObject);
    /* 重载析构：释放内部缓冲 */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX7Session_deinit);

    XCLASS_SHOW_SIZE_DEFAULT(XS7Session);
    return XVTABLE_DEFAULT;
}

// =============== 创建/初始化 ===============
XS7Session* XS7Session_create_ex(XMemoryType memory)
{
    XS7Session* session = (XS7Session*)XMemory_malloc(sizeof(XS7Session), memory);
    if (session) {
        XS7Session_init(session);
        Set_Class_Memory(session, memory);
        Set_Class_IsHeap(session, true);
    }
    return session;
}

void XS7Session_init(XS7Session* session)
{
    if (!session) return;

    XObject_init((XObject*)session);
    XClassGetVtable(session) = XS7Session_class_init();

    session->m_state = XS7Session_Idle;
    session->m_localTsap = 0x0100;
    session->m_remoteTsap = 0x0100;
    session->m_requestedPduLen = XS7_REQUESTED_PDU_LEN;
    session->m_amq = XS7_AMQ;
    session->m_negotiatedPduLen = 0;
    session->m_nextPduRef = 1;
    session->m_pendingEvent = XS7SessionEv_None;
    session->m_buffer = XByteArray_create();
    session->m_frame = XByteArray_create();
}

// =============== 析构 ===============
static void VX7Session_deinit(XS7Session* session)
{
    if (!session) return;

    if (session->m_buffer) {
        XClassDelete(session->m_buffer);
        session->m_buffer = NULL;
    }
    if (session->m_frame) {
        XClassDelete(session->m_frame);
        session->m_frame = NULL;
    }
    session->m_pendingEvent = XS7SessionEv_None;

    XClass_Deinit_Parent(XObject, session);
}

// =============== 会话 API ===============
void XS7Session_setParameters(XS7Session* session, uint16_t localTsap, uint16_t remoteTsap,
                              uint16_t requestedPduLen, uint16_t amq)
{
    if (!session) return;
    session->m_localTsap = localTsap;
    session->m_remoteTsap = remoteTsap;
    session->m_requestedPduLen = requestedPduLen;
    session->m_amq = amq;
}

XS7Session_State XS7Session_state(const XS7Session* session)
{
    return session ? (XS7Session_State)session->m_state : XS7Session_Idle;
}

uint16_t XS7Session_negotiatedPduSize(const XS7Session* session)
{
    return session ? session->m_negotiatedPduLen : 0;
}

uint16_t XS7Session_nextPduRef(XS7Session* session)
{
    if (!session) return 0;
    uint16_t ref = session->m_nextPduRef;
    session->m_nextPduRef = (uint16_t)(ref + 1);
    if (session->m_nextPduRef == 0) session->m_nextPduRef = 1;   /* 1..65535 循环 */
    return ref;
}

bool XS7Session_buildNextHandshake(XS7Session* session, XByteArray* outFrame)
{
    if (!session || !outFrame) return false;

    switch (session->m_state) {
    case XS7Session_Idle: {
        /* 第一步：COTP 连接请求（含 TPKT） */
        XByteArray_clear_base(outFrame);
        if (!XS7Cotp_buildCr(outFrame, session->m_localTsap, session->m_remoteTsap, 0x0A)) {
            return false;
        }
        session->m_state = XS7Session_CrSent;
        return true;
    }

    case XS7Session_CrSent: {
        /* 第二步：S7 SetupCommunication 请求（客户端在收到 CcAccepted 后调用） */
        uint8_t pdu[32];
        uint16_t pduRef = XS7Session_nextPduRef(session);
        size_t pduLen = XS7Pdu_buildSetupCommunication(pdu, pduRef,
                                                       session->m_amq,
                                                       session->m_requestedPduLen);
        if (pduLen == 0) return false;

        uint8_t dt[40];
        dt[0] = 0x02; dt[1] = XS7COTP_PDU_DT; dt[2] = 0x80;   /* COTP DT 头 */
        memcpy(dt + 3, pdu, pduLen);

        XByteArray_clear_base(outFrame);
        if (XS7Tpkt_wrap(outFrame, dt, 3 + pduLen) == 0) {
            return false;
        }
        session->m_state = XS7Session_SetupSent;
        return true;
    }

    default:
        return false;   /* SetupSent/Ready/Error：无待发握手帧 */
    }
}

bool XS7Session_feed(XS7Session* session, const uint8_t* data, size_t len)
{
    if (!session || !session->m_buffer) return false;
    if (!data || len == 0) return true;

    if (!XByteArray_append_2(session->m_buffer, data, len)) {
        return false;
    }
    xs7SessionParseMore(session);
    return true;
}

XS7Session_EventType XS7Session_takeEvent(XS7Session* session)
{
    if (!session) return XS7SessionEv_None;

    /* 上一个 S7Frame 的借用数据已按契约读完，可安全解析下一帧补槽 */
    xs7SessionParseMore(session);

    XS7Session_EventType ev = (XS7Session_EventType)session->m_pendingEvent;
    session->m_pendingEvent = XS7SessionEv_None;
    return ev;
}

const uint8_t* XS7Session_frame(XS7Session* session, size_t* len)
{
    if (!session || !session->m_frame || XByteArray_size_base(session->m_frame) == 0) {
        if (len) *len = 0;
        return NULL;
    }
    if (len) *len = XByteArray_size_base(session->m_frame);
    return XByteArray_data(session->m_frame);
}

void XS7Session_reset(XS7Session* session)
{
    if (!session) return;

    if (session->m_buffer) XByteArray_clear_base(session->m_buffer);
    if (session->m_frame) XByteArray_clear_base(session->m_frame);
    session->m_state = XS7Session_Idle;
    session->m_pendingEvent = XS7SessionEv_None;
    session->m_negotiatedPduLen = 0;
    session->m_nextPduRef = 1;
    /* 保留 m_localTsap/m_remoteTsap/m_requestedPduLen/m_amq */
}

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */
