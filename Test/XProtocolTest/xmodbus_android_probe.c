/**
 * @file       xmodbus_android_probe.c
 * @brief      安卓真机 Modbus TCP 主站轮询探针（独立可执行）。
 * @details    [2026-10-25] 安卓真机（小米 25102RKBEC，kernel 6.12）上，
 *             Modbus 主站连真实远程 IO 设备（192.168.1.117:502）时：
 *             TCP 层 ESTABLISHED，但界面「请求: 0 错误: 0」——因为统计行
 *             只在**收到应答时**刷新（xgui_demo_page_modbus.c:1855 在
 *             mb_masterOnReply 末尾），零应答 ⇒ 计数与标签都不动。
 *
 *             根因方向：安卓 seccomp 拒 io_uring_setup（实测 EACCES，
 *             非代码注释预期的 ENOSYS/SIGSYS），故强制走
 *             XNetIoRingPosix 的 **epoll 引擎**；桌面走 io_uring——
 *             两条路径不同，桌面全绿掩盖了 epoll 分支缺陷。
 *
 *             本探针不含 GUI，严格复刻 demo 主站页的四相位轮询
 *             （DI→DO→IR→HR）+ 自动重连 + 单事务写让路。
 *
 *             ⚠ 槽函数约定（Src/XCode/XEvent/XSignalSlot/XSignalSlot.h:16-17）：
 *               XSlotFunc1 第一个参数是 **receiver**（连接时传入的对象），
 *               XSlotFunc2 第一个参数才是 **sender**（信号发射者）。
 *               本探针一律用 connect_2 取真 sender——早期版本误用
 *               connect_1 把 XModbusTcpClient 当 XModbusReply 读，
 *               读出 state=2/values=0 等错位值并误删 client，
 *               造成「假卡死」现场（2026-10-25 已订正）。
 *
 *             用法（安卓）：
 *               ./xmodbus_android_probe <host> <port> <unitId> <pointCount>
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XModbusTcpClient.h"
#include "XModbusTcpClient_Protected.h"
#include "XModbusClient.h"
#include "XModbusDataUnit.h"
#include "XModbusReply.h"
#include "XModbusDevice.h"
#include "XIODevice.h"
#include "XMemory.h"
#include "XTimer.h"
#include "XVariant.h"
#include "XString.h"
#include "XByteArray.h"

#define PROBE_MAX_TICKS 15

/* [2026-10-25 soak 长跑模式] 桌面双引擎（io_uring/epoll）断线重连回归用：
 *   ./xmodbus_android_probe <host> <port> <unit> <points> soak [秒]
 * 秒数省略或 0 = 跑到被杀为止。每拍带单调毫秒时间戳，统计
 * 断链次数与恢复次数（断链后首个 OK 应答 = 恢复），每 10 拍打一行汇总。 */

/* 相位：0=DI(FC2) 1=DO(FC1) 2=IR(FC4) 3=HR(FC3) ——同 demo 主站页。 */
typedef enum {
    PHASE_DI = 0,
    PHASE_DO = 1,
    PHASE_IR = 2,
    PHASE_HR = 3
} ProbePhase;

static XModbusTcpClient* g_client = NULL;
static XModbusReply*     g_reply  = NULL;   /* 在途请求（复刻 demo 的 pendingReply）。 */
static int  g_tick        = 0;
static int  g_done        = 0;
static int  g_okCount     = 0;
static int  g_errCount    = 0;
static int  g_unitId      = 1;
static int  g_pointCount  = 8;
static int  g_irCount     = 16;
static int  g_hrCount     = 16;
static ProbePhase g_phase  = PHASE_DI;
static char g_host[128]   = "192.168.1.117";
static int  g_port        = 502;

/* soak 模式状态。 */
static int      g_soak        = 0;
static long     g_soakSeconds = 0;   /* 0 = 无限 */
static uint64_t g_bootMs      = 0;
static int      g_discCount   = 0;  /* 观测到的断链次数（state→Unconnected） */
static int      g_recovCount  = 0;  /* 断链后首个 OK 应答次数 */
static int      g_offline     = 0;  /* 当前是否处于断链态 */
static int      g_nullStreak  = 0;  /* 连续 sendReadRequest 返回 NULL 拍数 */
static uint64_t g_lastStateMs = 0;  /* 上次状态变化时刻（恢复耗时计算） */

static uint64_t nowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000u);
}

/* 前向声明。 */
static void onReply(XObject* sender, XVarList* args);
static void onStateChanged(XObject* sender, XVarList* args);
static void onTick(void* userData, XTimerData* timer);

static void probe_log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

/** @brief 应答到达（connect_2 → 第一参是真 sender = reply）。 */
static void onReply(XObject* sender, XVarList* args)
{
    (void)args;
    XModbusReply* reply = (XModbusReply*)sender;
    if (!reply) return;
    if (reply == g_reply) g_reply = NULL;

    XModbusDevice_Error err = XModbusReply_error(reply);
    XString* errStr = XModbusReply_errorString(reply);
    probe_log("[REPLY #%d t=%llu] state=%d error=%d errStr='%s'\n",
              g_tick, (unsigned long long)(nowMs() - g_bootMs),
              (int)XModbusReply_state(reply), (int)err,
              errStr ? XString_toUtf8(errStr) : "");

    if (err == XModbusDevice_NoError) {
        const XModbusDataUnit* result = XModbusReply_result_const(reply);
        size_t n = result ? XModbusDataUnit_valueCount(result) : 0;
        ++g_okCount;
        probe_log("[REPLY #%d t=%llu] OK  values=%u  first=%u\n",
                  g_tick, (unsigned long long)(nowMs() - g_bootMs),
                  (unsigned)n,
                  (n ? XModbusDataUnit_value(result, 0) : 0));
        if (g_offline) {
            g_offline = 0;
            ++g_recovCount;
            probe_log("[RECOVERED #%d t=%llu] 断链后恢复（耗时 %llu ms, 累计 disc=%d recov=%d）\n",
                      g_recovCount, (unsigned long long)(nowMs() - g_bootMs),
                      (unsigned long long)(nowMs() - g_lastStateMs),
                      g_discCount, g_recovCount);
        }
    } else {
        ++g_errCount;
    }
    XObject_deleteLater((XObject*)reply);
    if (!g_soak && g_tick >= PROBE_MAX_TICKS) g_done = 1;
}

/** @brief 设备状态变化。 */
static void onStateChanged(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        XVarList_args_1(args, int, state);
        g_lastStateMs = nowMs();
        probe_log("[STATE t=%llu] device state -> %d\n",
                  (unsigned long long)(g_lastStateMs - g_bootMs), state);
        /* 0=Unconnected 1=Connecting 2=Connected（XModbusDevice_State）。
         * soak 下断链判定：掉到 Unconnected/Connecting 且此前在线。 */
        if (state != 2 && !g_offline && g_okCount > 0) {
            g_offline = 1;
            ++g_discCount;
            probe_log("[DISCONNECT #%d t=%llu] 在线→离线（state=%d）\n",
                      g_discCount, (unsigned long long)(g_lastStateMs - g_bootMs),
                      state);
        }
    }
}

/** @brief 每相位一个**缓存复用**的数据单元。
 *  @details 严格复刻 demo 主站页：pollUnit/coilUnit/irUnit/hrUnit 均
 *  **首次创建后反复复用**（xgui_demo_page_modbus.c:1707-1723），
 *  不每次重建。 */
static XModbusDataUnit* g_unitCache[4] = { NULL, NULL, NULL, NULL };

/** @brief 取得当前相位的单元（首次创建，之后复用）。 */
static XModbusDataUnit* getUnit(ProbePhase phase)
{
    if (g_unitCache[phase]) return g_unitCache[phase];
    XModbusDataUnit* unit = NULL;
    switch (phase) {
    case PHASE_DI:
        unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         XModbusDiscreteInputs, 0,
                                         (size_t)g_pointCount);
        break;
    case PHASE_DO:
        unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         XModbusCoils, 0,
                                         (size_t)g_pointCount);
        break;
    case PHASE_IR:
        unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         XModbusInputRegisters, 0,
                                         (size_t)g_irCount);
        break;
    case PHASE_HR:
    default:
        unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         XModbusHoldingRegisters, 0,
                                         (size_t)g_hrCount);
        break;
    }
    g_unitCache[phase] = unit;
    return unit;
}

/** @brief 每拍状态汇总（soak 用）：连续 NULL 发送是「卡死」征兆。 */
static void soakSummary(int nullNow)
{
    g_nullStreak = nullNow ? (g_nullStreak + 1) : 0;
    if (g_nullStreak == 3)
        probe_log("[STUCK?] 连续 %d 拍发送失败（state=%d offline=%d）\n",
                  g_nullStreak,
                  (int)XModbusDevice_state((XModbusDevice*)g_client),
                  g_offline);
    if (g_tick % 10 == 0)
        probe_log("[SUMMARY t=%llu] tick=%d ok=%d err=%d disc=%d recov=%d nullStreak=%d\n",
                  (unsigned long long)(nowMs() - g_bootMs), g_tick,
                  g_okCount, g_errCount, g_discCount, g_recovCount,
                  g_nullStreak);
}

/** @brief 轮询拍：每 1s 发一次读请求（四相位轮转）。 */
static void onTick(void* userData, XTimerData* timer)
{
    (void)userData; (void)timer;
    if (g_done || !g_client) return;

    ++g_tick;
    if (g_reply) {
        probe_log("[TICK %d t=%llu] 在途应答未回（pendingReply 卡住）→ 不发新请求\n",
                  g_tick, (unsigned long long)(nowMs() - g_bootMs));
        return;
    }

    XModbusDevice_State st = XModbusDevice_state((XModbusDevice*)g_client);
    XModbusDataUnit* unit = getUnit(g_phase);
    if (!unit) { probe_log("[TICK %d] unit 创建失败\n", g_tick); return; }

    probe_log("[TICK %d t=%llu] state=%d phase=%d sendRead...\n",
              g_tick, (unsigned long long)(nowMs() - g_bootMs),
              (int)st, (int)g_phase);

    /* 注意：**不**删除 unit（缓存复用，与 demo 一致）。 */
    XModbusReply* reply = XModbusClient_sendReadRequest(
        (XModbusClient*)g_client, unit, g_unitId);

    if (!reply) {
        ++g_errCount;
        probe_log("[TICK %d t=%llu] sendReadRequest 返回 NULL（未连接或发送失败）\n",
                  g_tick, (unsigned long long)(nowMs() - g_bootMs));
        g_phase = (ProbePhase)((g_phase + 1) % 4);
        if (g_soak) soakSummary(1);
        if (!g_soak && g_tick >= PROBE_MAX_TICKS) g_done = 1;
        return;
    }
    if (g_soak) soakSummary(0);
    g_reply = reply;
    /* connect_2：第一参是真 sender（reply 本身）。 */
    XObject_connect_2((XObject*)reply,
                      XSignal(XModbusReply_finished_signal), onReply);
    g_phase = (ProbePhase)((g_phase + 1) % 4);
}

/** @brief 第二轮连接：先断开、再用同一 client 重新 connectDevice。
 *  @details 复刻 demo 页「连接 → 断开 → 再连接」序列，验证**进程内状态
 *  残留**是否会让第二次连接失效（安卓「重启即恢复」⇒ 强烈指向进程内
 *  状态，而非库/epoll/设备/网络）。
 *  用法：./xmodbus_android_probe <host> <port> <unitId> <pointCount> reconnect */
static void onReconnect(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    probe_log("[RECONNECT] 断开并重连：disconnectDevice + connectDevice\n");
    if (!g_client) return;
    g_reply = NULL;
    g_phase = PHASE_DI;

    /* 断开（复刻 demo 的 stopEngine 语义）。 */
    XModbusDevice_disconnectDevice((XModbusDevice*)g_client);

    /* 同 client 重新连接。 */
    bool ok = XModbusDevice_connectDevice((XModbusDevice*)g_client);
    probe_log("[RECONNECT] connectDevice -> %d\n", (int)ok);
}

int main(int argc, char* argv[])
{
    if (argc > 1) snprintf(g_host, sizeof(g_host), "%s", argv[1]);
    if (argc > 2) g_port = atoi(argv[2]);
    if (argc > 3) g_unitId = atoi(argv[3]);
    if (argc > 4) g_pointCount = atoi(argv[4]);
    if (argc > 5 && strcmp(argv[5], "soak") == 0) {
        g_soak = 1;
        if (argc > 6) g_soakSeconds = atol(argv[6]);
    }
    g_bootMs = nowMs();

    probe_log("=== Modbus TCP master probe (demo-equivalent) ===\n");
    probe_log("host=%s port=%d unit=%d points=%d soak=%d%s%ld%s\n",
              g_host, g_port, g_unitId, g_pointCount, g_soak,
              g_soak ? " seconds=" : "", g_soakSeconds,
              (argc > 5 && strcmp(argv[5], "reconnect") == 0) ? " mode=reconnect" : "");

    XCoreApplication_create(0, NULL);

    g_client = XModbusTcpClient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!g_client) { probe_log("client create failed\n"); return 2; }

    /* [复刻 demo 主站页] 自动重连开启 + 2000ms 间隔。 */
    XModbusClient_setAutoReconnect((XModbusClient*)g_client, true);
    XModbusClient_setReconnectInterval((XModbusClient*)g_client, 2000);
    XModbusDevice_setConnectionParameter_ref(
        (XModbusDevice*)g_client, XModbusDevice_NetworkAddressParameter,
        XVariant_create_utf8_str(g_host));
    XModbusDevice_setConnectionParameter_ref(
        (XModbusDevice*)g_client, XModbusDevice_NetworkPortParameter,
        XVariant_create_int(g_port));

    XObject_connect_1((XObject*)g_client,
                      XSignal(XModbusDevice_stateChanged_signal),
                      (XObject*)g_client, onStateChanged, XConnectionType_Direct);

    /* [状态残留实验] 复刻 demo 页「连接 → 断开 → 再连接」序列：
     * 第 6 拍断开，第 7 拍用**同一个 client** 重新连接，观察第二次
     * 连接是否仍能收到应答（安卓「重启即恢复」指向进程内状态残留）。 */
    if (argc > 5 && strcmp(argv[5], "reconnect") == 0) {
        XTimer* rc = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XTimer_setInterval(rc, 1000);
        XTimer_setTimerCallback(rc, onReconnect);
        XTimer_setUserData(rc, NULL);
        XTimer_start_base(rc);
    }

    if (!XModbusDevice_connectDevice((XModbusDevice*)g_client)) {
        probe_log("connectDevice 返回 false\n");
        return 3;
    }
    probe_log("connectDevice 已发起（异步）\n");

    XTimer* timer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!timer) { probe_log("timer create failed\n"); return 5; }
    XTimer_setInterval(timer, 1000);
    XTimer_setTimerCallback(timer, onTick);
    XTimer_setUserData(timer, NULL);
    XTimer_start_base(timer);

    while (!g_done) {
        if (g_soak) {
            XCoreApplication_processEvents(XEventLoop_AllEvents |
                                           XEventLoop_WaitForMoreEvents);
            if (g_soakSeconds > 0 &&
                (long)((nowMs() - g_bootMs) / 1000u) >= g_soakSeconds)
                break;
            continue;
        }
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }

    if (g_soak) {
        probe_log("=== SOAK 结果: ok=%d err=%d ticks=%d disc=%d recov=%d elapsed=%llu ms ===\n",
                  g_okCount, g_errCount, g_tick, g_discCount, g_recovCount,
                  (unsigned long long)(nowMs() - g_bootMs));
        /* 验收判据：断链次数与恢复次数一致且均 >0 = 全部自愈。 */
        if (g_discCount > 0 && g_discCount == g_recovCount) return 0;
        if (g_discCount > 0) return 1; /* 有断链未恢复 */
        return g_okCount > 0 ? 0 : 1;
    }
    probe_log("=== 结果: ok=%d err=%d ticks=%d ===\n",
              g_okCount, g_errCount, g_tick);
    return g_okCount > 0 ? 0 : 1;
}
