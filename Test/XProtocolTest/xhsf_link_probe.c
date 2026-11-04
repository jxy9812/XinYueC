
/** @file       xhsf_link_probe.c
 * @brief       HSF 服务端联调探针：登录 → 遥测快照 → 控制命令 → 角色门禁。
 * @details     对真实 HsfServer 做全链验证（XLink 挑战应答 + BSON 载荷 +
 *              XVfdCodec 解码）：
 *                [1] 以 argv 指定凭据登录（user:pass@host:port）；
 *                [2] VFD_SNAPSHOT / AI_SNAPSHOT：Response 解码打印遥测；
 *                [3] VFD_CONTROL 停机（安全动作）：打印应答包络（root
 *                    应受理 ok=true；admin 应被拒 ok=false「权限不足」）；
 *                [4] 三步齐了退出：全部预期 → 0；任一不符 → 1。
 *              用法：xhsf_link_probe.exe admin:admin@127.0.0.1:6100
 *                   xhsf_link_probe.exe root:root@127.0.0.1:6100
 */
#include "CXinYueConfig.h"

#if XPROTOCOL_ON

#include "XhsfLinkProto.h"
#include "XLinkClient.h"
#include "XLinkSession.h"
#include "XLinkProto.h"
#include "XVfdCodec.h"
#include "XBson.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XCoreApplication.h"
#include "XObject.h"
#include "XVarList.h"
#include "XByteArray.h"
#include "XMemory.h"
#include "XDateTime.h"
#include "XPrintf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/** @brief 探针阶段位（ready 后依次推进）。 */
static int s_phase = 0;             /**< 0=待登录 1=快照 2=控制 3=完成。 */
static bool s_snapGot = false;      /**< VFD 快照已收。 */
static bool s_aiGot = false;        /**< AI 快照已收。 */
static bool s_tempHumGot = false;   /**< 温湿度快照已收（且至少一路有效）。 */
static bool s_ctrlOk = false;       /**< 控制应答已收。 */
static bool s_ctrlAccepted = false; /**< 控制被受理（预期随角色）。 */
static XLinkClient* s_cli = NULL;   /**< 客户端。 */
static char s_user[64];             /**< 用户名（判定预期角色）。 */
static uint64_t s_deadlineMs = 0;   /**< 整体截止。 */
static int s_ctrlMode = 0;          /**< 1=控制序列 2=单发频率。 */
static double s_setFreqHz = 0.0;    /**< 单发频率值（Hz）。 */
static int s_ctrlStep = 0;          /**< 序列步号。 */
static int s_ctrlTick = 0;          /**< 当前步拍数。 */
static double s_freqPeak = 0.0;     /**< 启动后频率峰值。 */
static double s_freqLow = -1.0;     /**< 改频后频率（停机前最后一拍）。 */
static int s_finalState = -1;       /**< 停机后状态字。 */

/**
 * @brief 发一拍快照请求（VFD + AI）。
 */
static void probeRequestSnapshots(void)
{
    XLinkSession* session = XLinkClient_session(s_cli);
    uint32_t reqId = 0;
    if (!session) return;
    XLinkSession_sendRequest_ex(session, HSF_CMD_VFD_SNAPSHOT,
                                XLinkDataType_Bson, NULL, 0, &reqId);
    XLinkSession_sendRequest_ex(session, HSF_CMD_AI_SNAPSHOT,
                                XLinkDataType_Bson, NULL, 0, &reqId);
    /* 温湿度：载荷带 side=0（A 侧）；键名见 XhsfLinkProto.h。 */
    {
        XBsonDocument* doc = XBsonDocument_create();
        if (doc) {
            XByteArray* bytes;
            XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_SIDE, HSF_SIDE_A);
            bytes = XBsonDocument_toBson(doc);
            XClassDelete((XClass*)doc);
            if (bytes) {
                XLinkSession_sendRequest_ex(session, HSF_CMD_TEMPHUM_SNAPSHOT,
                                            XLinkDataType_Bson,
                                            (const uint8_t*)XByteArray_constData(bytes),
                                            (uint32_t)XByteArray_size_base(bytes),
                                            &reqId);
                XClassDelete((XClass*)bytes);
            }
        }
    }
}

/**
 * @brief 发一条控制请求（--ctrl 序列用）。
 * @param op 动作；reverse 方向；hz 频率（SetFrequency 用）。
 */
static void probeSendCtrlOp(XVfdOperation op, bool reverse, double hz)
{
    XLinkSession* session = XLinkClient_session(s_cli);
    XVfdControlRequest req;
    XByteArray* bytes;
    uint32_t reqId = 0;
    if (!session) return;
    memset(&req, 0, sizeof(req));
    req.m_op = op;
    req.m_reverse = reverse;
    req.m_valueHz = hz;
    bytes = XVfdCodec_controlToBson(&req);
    if (!bytes) return;
    XLinkSession_sendRequest_ex(session, HSF_CMD_VFD_CONTROL,
                                XLinkDataType_Bson,
                                (const uint8_t*)XByteArray_constData(bytes),
                                (uint32_t)XByteArray_size_base(bytes), &reqId);
    XClassDelete((XClass*)bytes);
}

/**
 * @brief 发控制命令（停机——安全动作）。
 */
static void probeSendControl(void)
{
    XLinkSession* session = XLinkClient_session(s_cli);
    XVfdControlRequest req;
    XByteArray* bytes;
    uint32_t reqId = 0;
    if (!session) return;
    memset(&req, 0, sizeof(req));
    req.m_op = XVfdOperation_Stop;
    bytes = XVfdCodec_controlToBson(&req);
    if (!bytes) return;
    XLinkSession_sendRequest_ex(session, HSF_CMD_VFD_CONTROL,
                                XLinkDataType_Bson,
                                (const uint8_t*)XByteArray_constData(bytes),
                                (uint32_t)XByteArray_size_base(bytes), &reqId);
    XClassDelete((XClass*)bytes);
}

/**
 * @brief 打印 VFD 遥测（解码后关键量）。
 */
static void probePrintVfd(const XVfdStatus* st)
{
    XPrintf("[probe] VFD: valid=%d runState=%d running=%d faulted=%d "
            "setFreq=%.1fHz outFreq=%.1fHz current=%.2fA bus=%.1fV fault=0x%04X\n",
            (int)st->m_valid, (int)st->m_runState, (int)st->m_running,
            (int)st->m_faulted, st->m_setFreqHz, st->m_outputFreqHz,
            st->m_outputCurrentA, st->m_busVoltageV, st->m_faultCode);
}

/**
 * @brief 打印 AI 遥测（8 路原始毫伏与换算 mA）。
 */
static void probePrintAi(const uint8_t* body, uint32_t len)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonValue* v;
    int n = 0;
    int i;
    if (!doc) return;
    if (XBsonDocument_from_bytes(doc, body, len)) {
        v = XBsonDocument_value_keyUtf8(doc, "n");
        if (v) n = XBsonValue_toInt32(v, 0);
        XPrintf("[probe] AI: %d 通道\n", n);
        for (i = 0; i < n && i < 8; ++i) {
            char key[12];
            int32_t raw = 0;
            int32_t ua = 0;
            snprintf(key, sizeof(key), "r%d", i);
            v = XBsonDocument_value_keyUtf8(doc, key);
            if (v) raw = XBsonValue_toInt32(v, 0);
            snprintf(key, sizeof(key), "u%d", i);
            v = XBsonDocument_value_keyUtf8(doc, key);
            if (v) ua = XBsonValue_toInt32(v, 0);
            XPrintf("[probe] AI CH%d: raw=%dmV  I=%.3fmA\n", i + 1, raw,
                    (double)ua / 1000.0);
        }
        s_aiGot = true;
    }
    XClassDelete((XClass*)doc);
}

/**
 * @brief --ctrl 序列推进（每拍一次；用遥测观测代替等应答）。
 */
static void probeCtrlStep(void)
{
    ++s_ctrlTick;
    switch (s_ctrlStep) {
    case 0:
        if (s_ctrlTick == 1) {
            XPrintf("[ctrl] 1) 设频 15.0Hz\n");
            probeSendCtrlOp(XVfdOperation_SetFrequency, false, 15.0);
        }
        if (s_ctrlTick >= 8) { s_ctrlStep = 1; s_ctrlTick = 0; }
        break;
    case 1:
        if (s_ctrlTick == 1) {
            XPrintf("[ctrl] 2) 正转启动\n");
            probeSendCtrlOp(XVfdOperation_Start, false, 0.0);
        }
        if (s_ctrlTick >= 40) { s_ctrlStep = 2; s_ctrlTick = 0; }
        break;
    case 2:
        if (s_ctrlTick == 1)
            XPrintf("[ctrl] 3) 运行中改给定 5.0Hz\n");
        if (s_ctrlTick == 2)
            probeSendCtrlOp(XVfdOperation_SetFrequency, false, 5.0);
        if (s_ctrlTick >= 40) { s_ctrlStep = 3; s_ctrlTick = 0; }
        break;
    case 3:
        if (s_ctrlTick == 1)
            XPrintf("[ctrl] 4) 自由停机\n");
        if (s_ctrlTick == 2)
            probeSendCtrlOp(XVfdOperation_FreeStop, false, 0.0);
        if (s_ctrlTick >= 16) { s_ctrlStep = 4; s_ctrlTick = 0; }
        break;
    default:
        s_phase = 3;
        break;
    }
}

/**
 * @brief 业务帧分发（响应路由）。
 */
static void probeDispatch(XLinkSession* session, const XLinkFrameHeader* frame,
                          const uint8_t* payload)
{
    (void)session;
    if (!frame) return;
    if (frame->m_msgClass == XLinkMsgClass_Response) {
        XBsonDocument* doc = XBsonDocument_create();
        XBsonValue* v;
        bool ok = false;
        if (frame->m_msgCode == HSF_CMD_VFD_SNAPSHOT && frame->m_payloadLen > 0) {
            XVfdStatus st;
            if (XVfdCodec_statusFromBson(&st, payload, frame->m_payloadLen)) {
                probePrintVfd(&st);
                s_snapGot = true;
                if (s_ctrlMode) {
                    if (st.m_outputFreqHz > s_freqPeak)
                        s_freqPeak = st.m_outputFreqHz;
                    if (s_ctrlStep == 2)
                        s_freqLow = st.m_outputFreqHz;
                    s_finalState = (int)st.m_runState;
                }
            }
        } else if (frame->m_msgCode == HSF_CMD_AI_SNAPSHOT &&
                   frame->m_payloadLen > 0) {
            probePrintAi(payload, frame->m_payloadLen);
        } else if (frame->m_msgCode == HSF_CMD_TEMPHUM_SNAPSHOT && doc) {
            /* 温湿度快照：温度 0.1℃（可负）、湿度 0.1%RH。 */
            if (XBsonDocument_from_bytes(doc, payload, frame->m_payloadLen)) {
                int32_t temp = 0;
                int32_t hum = 0;
                int32_t side = 0;
                bool tv = false;
                bool hv = false;
                v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_SIDE);
                if (v) side = XBsonValue_toInt32(v, 0);
                v = XBsonDocument_value_keyUtf8(doc, "temp");
                if (v) temp = XBsonValue_toInt32(v, 0);
                v = XBsonDocument_value_keyUtf8(doc, "hum");
                if (v) hum = XBsonValue_toInt32(v, 0);
                v = XBsonDocument_value_keyUtf8(doc, "tempValid");
                if (v) tv = XBsonValue_toBool(v, false);
                v = XBsonDocument_value_keyUtf8(doc, "humValid");
                if (v) hv = XBsonValue_toBool(v, false);
                XPrintf("[probe] TEMPHUM(side=%d): %.1f degC / %.1f %%RH  "
                        "(valid t=%d h=%d)\n",
                        (int)side, (double)temp / 10.0, (double)hum / 10.0,
                        (int)tv, (int)hv);
                s_tempHumGot = tv || hv;
            }
        } else if (frame->m_msgCode == HSF_CMD_VFD_CONTROL && doc) {
            const char* err = "";
            if (XBsonDocument_from_bytes(doc, payload, frame->m_payloadLen)) {
                v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_OK);
                if (v) ok = XBsonValue_toBool(v, false);
                v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_ERR);
                if (v) {
                    const XString* s = XBsonValue_toString(v);
                    err = s ? XString_toUtf8(s) : "";
                }
                XPrintf("[probe] CONTROL: ok=%d err='%s'\n", (int)ok, err);
                s_ctrlOk = true;
                s_ctrlAccepted = ok;
            }
        }
        if (doc) XClassDelete((XClass*)doc);
    }
}

/**
 * @brief 就绪后推进阶段。
 */
static void probeOnStateChanged(XObject* sender, XVarList* args)
{
    int state = -1;
    if (!args) return;
    XVarList_start(args);
    state = XVarList_arg(args, int);
    (void)sender;
    if ((XLinkClientState)state == XLinkClient_ConnectedState &&
        s_cli && XLinkClient_isReady(s_cli) && s_phase == 0) {
        XLinkSession* session = XLinkClient_session(s_cli);
        XPrintf("[probe] 登录成功（%s）\n", XLinkClient_user_const(s_cli));
        if (session)
            XLinkSession_subscribe(session, HSF_TOPIC_VFD_STATUS);
        s_phase = 1;
        probeRequestSnapshots();
        if (s_ctrlMode == 2) {
            XPrintf("[ctrl] 只发频率给定 %.1fHz\n", s_setFreqHz);
            probeSendCtrlOp(XVfdOperation_SetFrequency, false, s_setFreqHz);
        }
        else if (s_ctrlMode == 3) {
            XPrintf("[ctrl] 单发正转启动\n");
            probeSendCtrlOp(XVfdOperation_Start, false, 0.0);
        } else if (s_ctrlMode == 4) {
            XPrintf("[ctrl] 单发减速停机\n");
            probeSendCtrlOp(XVfdOperation_Stop, false, 0.0);
        } else if (s_ctrlMode == 5) {
            XPrintf("[ctrl] 单发自由停机\n");
            probeSendCtrlOp(XVfdOperation_FreeStop, false, 0.0);
        }
        if (s_ctrlMode) {
            XPrintf("[ctrl] 序列开始（总时长约 20s）\n");
        }
    } else if ((XLinkClientState)state == XLinkClient_UnconnectedState &&
               s_phase == 0) {
        XPrintf("[probe] 登录失败\n");
        s_phase = 3;
    }
}

/**
 * @brief 认证失败。
 */
static void probeOnAuthFailed(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    XPrintf("[probe] 认证被拒（用户名/密码错误）\n");
    s_phase = 3;
}

int main(int argc, char** argv)
{
    XCoreApplication* app;
    const char* spec;
    char buf[256];
    char* pass;
    char* host;
    char* portSep;
    uint16_t port = (uint16_t)HSF_XLINK_DEFAULT_PORT;

    if (argc < 2) {
        XPrintf("用法: xhsf_link_probe user:pass@host[:port]\n");
        return 2;
    }
    spec = argv[1];
    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) return 2;

    snprintf(buf, sizeof(buf), "%s", spec);
    pass = strchr(buf, ':');
    host = strchr(buf, '@');
    if (!pass || !host || pass > host) {
        XPrintf("[probe] spec 非法\n");
        return 2;
    }
    *pass = '\0';
    *host = '\0';
    portSep = strrchr(host + 1, ':');
    if (portSep) {
        int p;
        *portSep = '\0';
        p = atoi(portSep + 1);
        if (p <= 0 || p > 65535) return 2;
        port = (uint16_t)p;
    }
    snprintf(s_user, sizeof(s_user), "%s", buf);
    /* 第二参数 "ctrl"：进控制序列模式（真机启停/调频验证）。 */
    if (argc > 2 && strcmp(argv[2], "ctrl") == 0)
        s_ctrlMode = 1;
    /* setfreq<N>：只发一条频率给定（NN Hz），用于验证写帧是否真上总线。 */
    if (argc > 2 && strncmp(argv[2], "setfreq", 7) == 0) {
        s_ctrlMode = 2;
        s_setFreqHz = atof(argv[2] + 7);
    } else if (argc > 2 && strcmp(argv[2], "start") == 0) {
        s_ctrlMode = 3;
    } else if (argc > 2 && strcmp(argv[2], "stop") == 0) {
        s_ctrlMode = 4;
    } else if (argc > 2 && strcmp(argv[2], "freestop") == 0) {
        s_ctrlMode = 5;
    }

    s_cli = XLinkClient_create();
    if (!s_cli) return 2;
    XLinkClient_setHostPort(s_cli, host + 1, port);
    XLinkClient_setDispatchCallback(s_cli, probeDispatch, NULL);
    XLinkClient_setCredential(s_cli, buf, pass + 1);
    XObject_connect_2((XObject*)s_cli, XSignal(XLinkClient_stateChanged_signal),
                      probeOnStateChanged);
    XObject_connect_2((XObject*)s_cli, XSignal(XLinkClient_authFailed_signal),
                      probeOnAuthFailed);
    if (!XLinkClient_connectToServer(s_cli)) {
        XPrintf("[probe] 连接发起失败\n");
        return 2;
    }
    s_deadlineMs = XDateTime_currentMSecsSinceEpoch() + 12000;
    /* --hold <秒>：判完仍保持连接 N 秒（双会话策略验证用）。 */
    {
        int holdSec = (argc > 2) ? atoi(argv[2]) : 0;
        if (holdSec > 0)
            s_deadlineMs += (uint64_t)holdSec * 1000ull;
    }

    /* 有界事件泵：阶段推进/超时都在这里判。 */
    while (XDateTime_currentMSecsSinceEpoch() < s_deadlineMs) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (s_ctrlMode >= 2) {
            static int s_wait2 = 0;
            if (++s_wait2 > 40) { s_phase = 3; break; }
        } else if (s_ctrlMode) {
            if (s_phase == 1) {
                static int s_tickDiv = 0;
                if (++s_tickDiv % 8 == 0)     /* 约 50ms/拍 → 8 拍 ≈ 400ms */
                    probeCtrlStep();
                if (s_phase >= 3)
                    break;
            }
        } else if (s_phase == 1 && s_snapGot && s_aiGot && s_tempHumGot) {
            s_phase = 2;
            probeSendControl();
        } else if (s_phase == 2 && s_ctrlOk) {
            s_phase = 3;
            /* hold 模式不 break：保持连接到截止时刻（双会话策略验证）。 */
            if (argc <= 2 || atoi(argv[2]) <= 0)
                break;
        }
        XThread_msleep(5);
    }

    /* ctrl 模式：按频率轨迹判定（真机启停/调频是否真的生效）。 */
    if (s_ctrlMode) {
        int fail = 0;
        XPrintf("[ctrl] 结果: 频率峰值=%.1fHz  改频后=%.1fHz  结束状态=%d\n",
                s_freqPeak, s_freqLow, s_finalState);
        if (s_freqPeak <= 0.0) {
            XPrintf("[ctrl] [FAIL] 启动后运行频率始终为 0（命令未生效）\n");
            fail = 1;
        }
        if (s_freqPeak < 10.0) {
            XPrintf("[ctrl] [FAIL] 频率峰值 %.1fHz 未达到给定 15Hz 附近\n",
                    s_freqPeak);
            fail = 1;
        }
        if (s_freqLow >= 0.0 && s_freqLow > 8.0) {
            XPrintf("[ctrl] [FAIL] 运行中改 5Hz 后频率仍 %.1fHz（未跟随）\n",
                    s_freqLow);
            fail = 1;
        }
        if (s_finalState != (int)XVfdRunState_Stopped) {
            XPrintf("[ctrl] [FAIL] 停机后状态=%d（期望 Stopped=%d）\n",
                    s_finalState, (int)XVfdRunState_Stopped);
            fail = 1;
        }
        XPrintf("[ctrl] ===== %s =====\n", fail ? "FAIL" : "PASS");
        XLinkClient_disconnectFromServer(s_cli);
        XClassDelete((XClass*)s_cli);
        XClassDelete((XClass*)app);
        return fail ? 1 : 0;
    }
    /* 预期判定：root 控制应受理；admin（非管理角色）应被拒。 */
    {
        int fail = 0;
        if (!s_snapGot) { XPrintf("[probe] [FAIL] 未收到 VFD 快照\n"); fail = 1; }
        if (!s_aiGot)   { XPrintf("[probe] [FAIL] 未收到 AI 快照\n"); fail = 1; }
        if (!s_ctrlOk)  { XPrintf("[probe] [FAIL] 未收到控制应答\n"); fail = 1; }
        if (s_ctrlOk) {
            if (strcmp(s_user, "root") == 0 && !s_ctrlAccepted) {
                XPrintf("[probe] [FAIL] root 控制被拒\n");
                fail = 1;
            }
            if (strcmp(s_user, "root") != 0 && s_ctrlAccepted) {
                XPrintf("[probe] [FAIL] 非 root 控制竟被受理（权限门禁失效）\n");
                fail = 1;
            }
        }
        XPrintf("[probe] 结果: %s\n", fail ? "FAIL" : "PASS");
        XLinkClient_disconnectFromServer(s_cli);
        XClassDelete((XClass*)s_cli);
        XClassDelete((XClass*)app);
        return fail ? 1 : 0;
    }
}

#endif /* XPROTOCOL_ON */
