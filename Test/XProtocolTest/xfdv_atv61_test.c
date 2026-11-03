/**
 * @file       xfdv_atv61_test.c
 * @brief      ATV61 变频器 XVfd 通道化设备层闭环测试（端到端）。
 * @details    [2026-11-01 通道化重构] 拓扑改为：
 *               Python 模拟从站（mbtest_atv61_slave.py，虚拟串口 pty）
 *               → XModbusRtuSerialClient（总线主站）
 *               → XModbusHub（聚合轮询/写队列，虚拟地址空间）
 *               → XModbusPoint（数据源点）
 *               → XIoChannelGroup（通道组）
 *               → XVfdAtv61（设备层：语义槽 + CiA402 状态机）
 *             **设备层零 XModbus 依赖**：本文件负责装配，VFD 只见通道点。
 *             用例：
 *               ①串口上线（38400/8E1，pty 从端）→ Connected；
 *               ②轮询状态：Hub 段轮询 → ETA/电流/转矩/热状态快照回填；
 *               ③调速：写 50.0 Hz → 回读输出频率 50.0 Hz（0.1 Hz 定点）；
 *               ④启动：CiA402 状态机推进 → ETA bit2 置位（运行中）；
 *               ⑤停止：受控停机 → 轮询确认 ETA 回到状态 4（Switched on）；
 *               ⑥报警：注入故障 OCF(9) → ETA bit3 + LFT=9 + 故障名 OCF；
 *               ⑦故障复位 → 故障位清零、LFT=0。
 *             全部用例走统一事件循环（单线程，与库同款）；退出码=失败用例数。
 *
 *             ⚠ 信号槽约定：外部连接一律 connect_2（无 receiver 簿记）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XDateTime.h"
#include "XModbusTcpClient.h"
#include "XModbusDevice.h"
#include "XVariant.h"
#include "XString.h"
#include "xvfd_harness.h"
#include "xvfd_slave_emu.h"
#include "XVfd/XVfdAtv61.h"

#ifndef ATV61_BASE
#define ATV61_BASE "/tmp/mbsoak/atv61"
#endif

static int g_fail = 0;
static const char* g_case = "";
static uint64_t g_bootMs = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", msg); fflush(stdout); } \
    else { printf("  [FAIL] %s (%s@%d)\n", msg, g_case, __LINE__); fflush(stdout); ++g_fail; } \
} while (0)

static void tlog(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("[%8.3fs] ", (double)(XDateTime_currentMSecsSinceEpoch() - g_bootMs) / 1000.0);
    vprintf(fmt, ap); printf("\n"); fflush(stdout);
}

/* 从站模拟器（测试全局，泵循环内 tick）。 */
static XvfdSlaveEmu g_emu;
static XModbusHub* g_hub = NULL;        /* 调试用：Hub 缓存直读 */
static XModbusLink* g_link = NULL;      /* 调试用：role → 地址反查 */
static int g_netReads = 0;              /* 调试：从站被网络读次数 */
static int g_netWrites = 0;             /* 调试：从站被网络写次数 */
static char g_devErr[128] = { 0 };      /* 调试：客户端最近错误 */
static XVfdDevice* g_devbase = NULL;    /* 调试用：设备基类视图 */

typedef bool (*Pred)(void* ctx);
typedef bool (*StatusCond)(const XVfdStatus* st);

/** @brief 事件循环内等待状态快照满足条件（轮询解耦：按值等待）。 */
static bool waitStatus(StatusCond cond, int timeoutMs)
{
    static uint64_t dbgAt = 0;
    const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)timeoutMs;
    while (!cond(XVfdDevice_status_const(g_devbase))) {
        xvfdSlaveEmuTick(&g_emu);
        if (g_devbase && XDateTime_currentMSecsSinceEpoch() - dbgAt >= 400) {
            dbgAt = XDateTime_currentMSecsSinceEpoch();
            XIoPoint* pt = XVfdDevice_point(g_devbase, XVfdRole_StatusWord);
            uint16_t hv = 0;
            uint16_t va1 = 0;
            int ar1 = 0;
            /* [2026-11-02] 点不再持有链路地址：改由链路反查。 */
            if (pt && g_link && XModbusLink_queryBinding(g_link, XIoPoint_role(pt), &ar1, &va1))
                (void)XModbusHub_value(g_hub, XModbusLink_hubAreaType(ar1), va1, &hv);
            printf("[dbg-ws] hubETA=0x%04X devRaw=0x%04X running=%d emuState=%d emuCmd=0x%04X\n",
                   (unsigned)hv, (unsigned)XVfdDevice_status_const(g_devbase)->m_rawStateWord,
                   (int)XVfdDevice_status_const(g_devbase)->m_running,
                   g_emu.m_state, xvfdEmuGet(&g_emu, 8501));
            fflush(stdout);
        }
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        const uint64_t spin = XDateTime_currentMSecsSinceEpoch() + 2;
        while (XDateTime_currentMSecsSinceEpoch() < spin) { }
        if (XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    }
    return true;
}

static bool condSettled(const XVfdStatus* st)
{ return st && st->m_outputCurrentA == 100; }   /* 模拟器恒定电流：首拍到位 */
static bool condFreq50(const XVfdStatus* st)
{ return st && st->m_outputFreqHz > 49.9 && st->m_outputFreqHz < 50.1; }
static bool condRunning(const XVfdStatus* st)
{ return st && st->m_running; }
static bool condStopped(const XVfdStatus* st)
{ return st && !st->m_running &&
         (st->m_rawStateWord & XVfdAtv61_EtaMask) == XVfdAtv61_EtaStateSwitched; }
static bool condFault(const XVfdStatus* st)
{ return st && st->m_faulted && st->m_faultCode == 9; }
static bool condFaultCleared(const XVfdStatus* st)
{ return st && !st->m_faulted && st->m_faultCode == 0; }

/** @brief 事件循环内等待谓词成立。 */
static bool waitUntil(Pred pred, void* ctx, int timeoutMs)
{
    const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)timeoutMs;
    while (!pred(ctx)) {
        /* 从站模拟器必须随等待一起推进：命令字写落在从站寄存器后由
         * tick 消费（读命令→推状态机→回写 ETA）。漏 tick 会让等待期内
         * 从站冻结——写已上总线但无人处理，状态永远不迁移（2026-11-02 实锅：
         * 启动序列卡在第一步 9s 超时，根因即此处漏 tick）。 */
        xvfdSlaveEmuTick(&g_emu);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        const uint64_t spin = XDateTime_currentMSecsSinceEpoch() + 2;
        while (XDateTime_currentMSecsSinceEpoch() < spin) { }
        if (XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    }
    return true;
}

/** @brief 在超时内泵事件（无条件等待）。 */
static void pump(int ms)
{
    static uint64_t dbgAt = 0;
    const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)ms;
    while (XDateTime_currentMSecsSinceEpoch() < deadline) {
        xvfdSlaveEmuTick(&g_emu);
        if (XDateTime_currentMSecsSinceEpoch() - dbgAt >= 500) {
            dbgAt = XDateTime_currentMSecsSinceEpoch();
            XIoPoint* pt = g_devbase ? XVfdDevice_point(g_devbase, XVfdRole_StatusWord) : NULL;
            uint16_t va = 0;
            {
                int ar2 = 0;
                if (pt && g_link)
                    (void)XModbusLink_queryBinding(g_link, XIoPoint_role(pt), &ar2, &va);
            }
            uint16_t hv = 0;
            bool ok = (g_hub && pt) ? XModbusHub_value(g_hub, XModbusHoldingRegisters, va, &hv) : false;
            uint16_t hvRemote = 0;
            bool okRemote = (g_hub && pt) ? XModbusHub_value(g_hub, XModbusHoldingRegisters,
                                                             (uint16_t)(va + 3201), &hvRemote) : false;
            (void)okRemote;
            printf("[dbg] tick LCR=%u ETA=0x%04X | hub va=%u ok=%d val=%u ptValid=%d online=%d polling=%d groups=%d devState=%d netR=%d netW=%d %s remote(ok=%d val=%u)\n",
                   xvfdEmuGet(&g_emu, 3204), xvfdEmuGet(&g_emu, 3201),
                   (unsigned)va, (int)ok, (unsigned)hv,
                   pt ? (int)XIoPoint_isValid(pt) : -1,
                   g_hub ? (int)XModbusHub_isOnline(g_hub) : -1,
                   g_hub ? (int)XModbusHub_isPolling(g_hub) : -1,
                   g_hub ? (int)XModbusHub_readGroupCount(g_hub, 1) : -1,
                   g_hub ? (int)XModbusHub_deviceState(g_hub, 1) : -1,
                   g_netReads, g_netWrites, g_devErr,
                   (int)okRemote, (unsigned)hvRemote);
            fflush(stdout);
        }
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        const uint64_t spin = XDateTime_currentMSecsSinceEpoch() + 2;
        while (XDateTime_currentMSecsSinceEpoch() < spin) { }
    }
}

/* ==================== 设备信号计数 ==================== */
typedef struct {
    int statusCount;
    int doneCount;
    int lastOp;
    bool lastOk;
    int errorCount;
    char lastError[128];
} DrvCounters;

static DrvCounters g_drv;

static void onStatusUpdated(XObject* sender, XVarList* args)
{
    (void)sender;
    ++g_drv.statusCount;
    if (!args) return;
    XVarList_start(args);
    {
        const XVfdStatus* st = XVarList_arg(args, const XVfdStatus*);
        if (st) {
            tlog("  状态更新: raw=0x%04X state=%d f=%.1fHz I=%.0f T=%.1f%% fault=%u",
                 st->m_rawStateWord, (int)st->m_runState, st->m_outputFreqHz,
                 st->m_outputCurrentA, st->m_torquePercent, st->m_faultCode);
        }
    }
}

static void onCommandCompleted(XObject* sender, XVarList* args)
{
    (void)sender;
    ++g_drv.doneCount;
    if (!args) return;
    XVarList_start(args);
    g_drv.lastOp = (int)XVarList_arg(args, XVfdOperation);
    g_drv.lastOk = XVarList_arg(args, bool);
    tlog("  动作完成: op=%d ok=%d", g_drv.lastOp, (int)g_drv.lastOk);
}

static void onErrorOccurred(XObject* sender, XVarList* args)
{
    (void)sender;
    ++g_drv.errorCount;
    if (!args) return;
    XVarList_start(args);
    {
        const char* msg = XVarList_arg(args, const char*);
        if (msg) {
            snprintf(g_drv.lastError, sizeof(g_drv.lastError), "%s", msg);
            tlog("  设备错误: %s", msg);
        }
    }
}

static void onNetRead(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    ++g_netReads;
}

static void onNetWrite(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    ++g_netWrites;
}

static int gvc = 0;
static void onHubValueChanged(XObject* sender, XVarList* args)
{
    if (!args) return;
    XVarList_args_3(args, int, type, int, vStart, int, count);
    (void)sender;
    if (gvc < 14) {
        uint16_t v0 = 0, v1 = 0;
        XModbusHub_value(g_hub, (XModbusRegisterType)type, (uint16_t)vStart, &v0);
        if (count > 1) XModbusHub_value(g_hub, (XModbusRegisterType)type,
                                        (uint16_t)(vStart + 1), &v1);
        printf("[dbg-vc] type=%d vStart=%d count=%d v0=%u v1=%u\n",
               type, vStart, count, (unsigned)v0, (unsigned)v1);
        fflush(stdout);
    }
    ++gvc;
}

static void onDevError(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    {
        int e = (int)XVarList_arg(args, XModbusDevice_Error);
        snprintf(g_devErr, sizeof(g_devErr), "err=%d", e);
    }
}

static bool predDone(void* ctx) { (void)ctx; return g_drv.doneCount > 0; }
static bool predStatusGt(void* ctx) { return g_drv.statusCount > (int)(intptr_t)ctx; }
static bool predConnected(void* ctx)
{
    return XModbusDevice_state((XModbusDevice*)ctx) == XModbusDevice_ConnectedState;
}

int XVfdAtv61Test_runEx(const char* ptyOverride)
{
    XModbusTcpClient* client = NULL;
    XModbusDevice* mdev = NULL;
    uint16_t port = 0;
    XvfdHarness harness;
    XVfdAtv61* drive = NULL;
    XVfdDevice* base = NULL;
    const XVfdPointSpec* specs = NULL;
    int specCount = 0;
    int devId = -1;
    bool ok = false;

    memset(&harness, 0, sizeof(harness));
    g_bootMs = XDateTime_currentMSecsSinceEpoch();
    (void)ptyOverride;
    printf("=== ATV61 变频器 XVfd 通道化设备层闭环测试（进程内 TCP 从站）===\n"); fflush(stdout);
    XCoreApplication_create(0, NULL);

    /* ---------- ① 建链：TCP 从站（进程内）→ 客户端 → Hub → 通道组 ---------- */
    g_case = "topology";
    CHECK(xvfdSlaveEmuCreate(&g_emu, XVFD_EMU_ATV61, &port), "从站模拟器上线（自动端口）");
    if (!port) { printf("  [FAIL] 从站端口未取到\n"); ++g_fail; return 1; }
    tlog("从站端口: %u", (unsigned)port);

    client = XModbusTcpClient_create();
    CHECK(client != NULL, "TCP 客户端创建");
    if (!client) return 1;
    mdev = (XModbusDevice*)client;
    XModbusClient_setTimeout((XModbusClient*)client, 500);
    XModbusClient_setNumberOfRetries((XModbusClient*)client, 2);
    {
        XVariant* var = XVariant_create_utf8_str("127.0.0.1");
        if (var) XModbusDevice_setConnectionParameter(mdev,
            XModbusDevice_NetworkAddressParameter, var);
        XClassDelete((XClass*)var);
        var = XVariant_create_int((int)port);
        if (var) XModbusDevice_setConnectionParameter(mdev,
            XModbusDevice_NetworkPortParameter, var);
        XClassDelete((XClass*)var);
    }

    g_case = "connect";
    CHECK(XModbusDevice_connectDevice(mdev), "connectDevice 返回 true");
    CHECK(waitUntil(predConnected, mdev, 3000), "设备进入 ConnectedState");

    /* ---------- 装配：Hub 注入 + 通道组 + 点 + 设备绑定 ---------- */
    g_case = "assemble";
    CHECK(xvfdHarnessInit(&harness), "脚手架初始化（Hub/映射/通道组）");
    xvfdHarnessSetIdentity(&harness);   /* 单站：虚拟地址=品牌地址 */
    devId = xvfdHarnessAddStation(&harness, (XModbusClient*)client, 1, "atv61");
    CHECK(devId > 0, "从站注入 Hub（unit=1）");

    drive = XVfdAtv61_create();
    CHECK(drive != NULL, "XVfdAtv61 创建");
    if (!drive) goto cleanup;
    base = (XVfdDevice*)drive;
    CHECK(XVfdDevice_attach(base, xvfdHarnessStationGroup(&harness, devId, 1)),
          "设备挂接通道组");
    specs = XVfdAtv61_pointSpecs(&specCount);
    CHECK(xvfdHarnessMount(&harness, base, devId, 1, specs, specCount,
                           XVfdAtv61_modbusBinding()),
          "按品牌点表建点并绑定全部语义槽");
    CHECK(XVfdDevice_boundCount(base) == specCount, "语义槽绑定数 = 点表条目数");
    CHECK(XVfdDevice_hasCapability(base, XVfdCap_Reverse), "能力位图：反转");
    CHECK(!XVfdDevice_hasCapability(base, XVfdCap_Jog), "能力位图：无点动（ATV61 无通讯点动）");
    XVfdDevice_setStatusInterval(base, 100);
    XVfdAtv61_setStepDelay(drive, 120);

    XObject_connect_2((XObject*)drive, XSignal(XVfdDevice_statusUpdated_signal), onStatusUpdated);
    XObject_connect_2((XObject*)drive, XSignal(XVfdDevice_commandCompleted_signal), onCommandCompleted);
    XObject_connect_2((XObject*)drive, XSignal(XVfdDevice_errorOccurred_signal), onErrorOccurred);

    xvfdHarnessStart(&harness, 100);
    g_hub = harness.m_hub;
    g_link = harness.m_stations[0].m_link;   /* [2026-11-02 多站] 本站链路 */
    g_devbase = base;
    XObject_connect_2((XObject*)g_emu.m_server,
                      XSignal(XModbusServer_dataRead_signal), onNetRead);
    XObject_connect_2((XObject*)g_emu.m_server,
                      XSignal(XModbusServer_dataWritten_signal), onNetWrite);
    XObject_connect_2((XObject*)mdev, XSignal(XModbusDevice_errorOccurred_signal),
                      onDevError);
    XObject_connect_2((XObject*)g_hub, XSignal(XModbusHub_valueChanged_signal),
                      onHubValueChanged);
    tlog("轮询启动（Hub 段轮询 100ms）");

    /* ---------- ② 轮询状态 ---------- */
    g_case = "pollStatus";
    g_drv.statusCount = 0;
    ok = waitUntil(predStatusGt, (void*)(intptr_t)0, 6000);
    CHECK(ok, "轮询收到状态快照（Hub 段轮询→点推送→解码）");
    CHECK(waitStatus(condSettled, 6000), "轮询值到位（首拍零值→模拟器值）");
    {
        const XVfdStatus* st = XVfdDevice_status_const(base);
        CHECK(st && st->m_valid, "状态快照有效");
        CHECK(st && (st->m_rawStateWord & XVfdAtv61_EtaMask) == XVfdAtv61_EtaStateDisabled,
              "初始 ETA 状态 = 2（Switch on disabled）");
        CHECK(st && st->m_runState == XVfdRunState_Stopped, "运行状态映射 = Stopped");
        CHECK(st && st->m_outputCurrentA == 100, "电机电流回读 = 100");
        CHECK(st && st->m_torquePercent > 6.8 && st->m_torquePercent < 7.0,
              "电机转矩回读 ≈ 6.9%（0.1% 定点）");
        CHECK(st && st->m_driveThermalPercent > 0, "变频器热状态回读非零");
    }

    /* ---------- ③ 调速 ---------- */
    g_case = "setFrequency";
    g_drv.doneCount = 0;
    CHECK(XVfdDevice_setFrequency(base, 50.0), "发起写频率给定 50.0 Hz");
    CHECK(waitUntil(predDone, NULL, 4000), "写给定完成");
    CHECK(g_drv.lastOk && g_drv.lastOp == (int)XVfdOperation_SetFrequency, "写频率给定成功");
    CHECK(waitStatus(condFreq50, 6000), "输出频率回读 = 50.0 Hz（0.1Hz 定点）");

    /* ---------- ④ 启动（CiA402 序列） ---------- */
    g_case = "start";
    g_drv.doneCount = 0;
    CHECK(XVfdDevice_start(base, false), "发起启动");
    CHECK(waitUntil(predDone, NULL, 9000), "启动序列完成");
    CHECK(g_drv.lastOk && g_drv.lastOp == (int)XVfdOperation_Start, "启动成功");
    CHECK(waitStatus(condRunning, 6000), "轮询确认运行态");
    {
        const XVfdStatus* st = XVfdDevice_status_const(base);
        CHECK(st && st->m_running, "ETA bit2 置位（Operation enabled）");
        CHECK(st && (st->m_rawStateWord & XVfdAtv61_EtaMask) == XVfdAtv61_EtaStateRunning,
              "ETA 状态 = 5（Operation enabled）");
        CHECK(st && st->m_remote, "ETA bit9 置位（命令来自总线）");
    }

    /* ---------- ⑤ 停止 ---------- */
    g_case = "stop";
    g_drv.doneCount = 0;
    CHECK(XVfdDevice_stop(base), "发起受控停机");
    CHECK(waitUntil(predDone, NULL, 4000), "停机完成");
    CHECK(g_drv.lastOk, "停机成功");
    CHECK(waitStatus(condStopped, 6000), "轮询确认回到 Switched on");
    {
        const XVfdStatus* st = XVfdDevice_status_const(base);
        CHECK(st && !st->m_running, "停机后 ETA bit2 清零");
        CHECK(st && (st->m_rawStateWord & XVfdAtv61_EtaMask) == XVfdAtv61_EtaStateSwitched,
              "停机后 ETA 状态 = 4（Switched on）");
    }

    /* ---------- ⑥ 报警 ---------- */
    g_case = "fault";
    {
        char path[256];
        xvfdEmuAtv61InjectFault(&g_emu, 9);   /* OCF 过流 */
    }
    CHECK(waitStatus(condFault, 8000), "轮询读到故障态");
    {
        const XVfdStatus* st = XVfdDevice_status_const(base);
        CHECK(st && st->m_faulted, "ETA bit3 置位（故障）");
        CHECK(st && st->m_faultCode == 9, "LFT 故障码 = 9");
        CHECK(XVfdAtv61_faultName(9) != NULL && strcmp(XVfdAtv61_faultName(9), "OCF") == 0,
              "故障名映射 OCF");
        CHECK(XVfdAtv61_faultText(9) != NULL, "故障说明可查");
    }

    /* ---------- ⑦ 故障复位 ---------- */
    g_case = "resetFault";
    g_drv.doneCount = 0;
    CHECK(XVfdDevice_resetFault(base), "发起故障复位");
    CHECK(waitUntil(predDone, NULL, 4000), "复位动作完成");
    CHECK(g_drv.lastOk, "故障复位成功");
    CHECK(waitStatus(condFaultCleared, 8000), "轮询确认故障清除");
    {
        const XVfdStatus* st = XVfdDevice_status_const(base);
        CHECK(!st->m_faulted, "复位后 ETA bit3 清零");
        CHECK(st->m_faultCode == 0, "复位后 LFT = 0");
    }

    /* ---------- ⑧ 控制闸（统一修改许可） ---------- */
    g_case = "controlGate";
    {
        /* 缺省使能：不设置时行为与历史一致。 */
        CHECK(XVfdDevice_controlEnabled(base), "控制闸缺省使能");
        /* 关闸：修改动作全被拒，lastError=ControlDisabled。 */
        XVfdDevice_setControlEnabled(base, false);
        CHECK(!XVfdDevice_controlEnabled(base), "关闸生效");
        CHECK(!XVfdDevice_start(base, false), "关闸后 start 被拒");
        CHECK(XVfdDevice_lastError(base) == XVfdError_ControlDisabled,
              "关闸后 lastError = ControlDisabled");
        CHECK(!XVfdDevice_setFrequency(base, 25.0), "关闸后 setFrequency 被拒");
        CHECK(!XVfdDevice_resetFault(base), "关闸后 resetFault 被拒");
        /* 读路径不受影响：状态快照照常发布（闸只拦修改）。 */
        {
            const XVfdStatus* st = XVfdDevice_status_const(base);
            CHECK(st && st->m_valid, "关闸后读路径照常（状态快照仍有效）");
        }
        /* 开闸恢复：同型修改动作放行。 */
        XVfdDevice_setControlEnabled(base, true);
        CHECK(XVfdDevice_controlEnabled(base), "开闸生效");
        g_drv.doneCount = 0;
        CHECK(XVfdDevice_setFrequency(base, 30.0), "开闸后 setFrequency 放行");
        CHECK(waitUntil(predDone, NULL, 4000), "开闸后写给定完成");
        CHECK(g_drv.lastOk, "开闸后写频率给定成功");
    }

cleanup:
    if (base) XVfdDevice_detach(base);
    if (drive) XClassDelete((XClass*)drive);
    /* 设备已释放：调试视图指针必须同刻置空——下方 pump() 的调试取点会
     * 访问 XVfdDevice_point(g_devbase)，悬垂即 use-after-free
     * （2026-11-02 ASan 实锅）。 */
    g_devbase = NULL;
    if (harness.m_group || harness.m_hub) xvfdHarnessDestroy(&harness);
    if (mdev) XModbusDevice_disconnectDevice(mdev);
    if (client) XClassDelete((XClass*)client);
    xvfdSlaveEmuDestroy(&g_emu);
    pump(50);

    printf("=== 结束：失败用例 %d ===\n", g_fail);
    fflush(stdout);
    return g_fail;
}

int XVfdAtv61Test_run(void)
{
    /* pty 路径缺省读 ATV61_BASE/pty_path 文件（驱动脚本先行启动从站时写入）。 */
    return XVfdAtv61Test_runEx(NULL);
}

void XVfdAtv61Test(XVariant* data)
{
    const int fails = XVfdAtv61Test_run();
    (void)data;
    printf("=== XVfdAtv61Test: %s（失败用例 %d）===\n",
           fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}
