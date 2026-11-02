/**
 * @file       xfdv_m512_test.c
 * @brief      511M/512M/513M 双站混合总线 XVfd 通道化设备层闭环测试。
 * @details    [2026-11-01 通道化重构] 拓扑：
 *               Python 双站模拟器（mbtest_m512_slave.py，一条 pty 上两个站号）
 *               → XModbusRtuSerialClient（一个总线主站）
 *               → XModbusHub（两站各自映射段，虚拟地址隔离）
 *               → XModbusPoint × N → XIoChannelGroup → XVfdM512 × 2。
 *             覆盖：
 *               ①上线 + 双站快照（站号隔离）；
 *               ②调速：Hz→最大频率百分比→1000H（P0.10 换算）双站独立；
 *               ③启停：2000H 命令字 1/2/6 + 3000H 状态回读；
 *               ④故障：8000H 故障码 → 故障名映射 + 站间隔离；
 *               ⑤复位：2000H=7 → 故障清零；
 *               ⑥参数通道：P0.10 读（id=3）、P8.00 写后回读（id=11）；
 *               ⑦能力位/参数表/约束标注。
 *             退出码=失败用例数。
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
#include "XVfd/XVfdM512.h"

#ifndef M512_BASE
#define M512_BASE "/tmp/mbsoak/m512"
#endif

static int g_fail = 0;
static const char* g_case = "";
static uint64_t g_bootMs = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", msg); fflush(stdout); } \
    else { printf("  [FAIL] %s (%s@%d)\n", msg, g_case, __LINE__); fflush(stdout); ++g_fail; } \
} while (0)

/* 站点表（信号槽按 sender 区分站点）。 */
static XObject* g_devObj[2] = { NULL, NULL };
static XVfdDevice* g_dev[2] = { NULL, NULL };
/* 双站模拟器（每站独立 server/client，泵循环内 tick）。 */
static XvfdSlaveEmu g_emu[2];

typedef struct {
    int statusCount;
    int doneCount;
    int lastOp;
    bool lastOk;
    int paramReadCount;
    uint16_t paramReadId;
    int32_t paramReadRaw;
    bool paramReadOk;
    int paramWriteCount;
    uint16_t paramWriteId;
    bool paramWriteOk;
    int errorCount;
} DrvCounters;

static DrvCounters g_d[2];

static void tlog(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("[%8.3fs] ", (double)(XDateTime_currentMSecsSinceEpoch() - g_bootMs) / 1000.0);
    vprintf(fmt, ap); printf("\n"); fflush(stdout);
}

/** @brief 泵事件指定毫秒（单线程事件循环；两站模拟器同拍推进）。 */
static void pump(int ms)
{
    const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)ms;
    while (XDateTime_currentMSecsSinceEpoch() < deadline) {
        xvfdSlaveEmuTick(&g_emu[0]);
        xvfdSlaveEmuTick(&g_emu[1]);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        const uint64_t spin = XDateTime_currentMSecsSinceEpoch() + 2;
        while (XDateTime_currentMSecsSinceEpoch() < spin) { }
    }
}

/** @brief sender → 站点下标。 */
static int devIndex(XObject* sender)
{
    if (sender && sender == g_devObj[1]) return 1;
    return 0;
}

static void onStatusUpdated(XObject* sender, XVarList* args)
{
    (void)args;
    ++g_d[devIndex(sender)].statusCount;
}

static void onCommandCompleted(XObject* sender, XVarList* args)
{
    DrvCounters* c = &g_d[devIndex(sender)];
    if (!args) return;
    XVarList_start(args);
    ++c->doneCount;
    c->lastOp = (int)XVarList_arg(args, XVfdOperation);
    c->lastOk = XVarList_arg(args, bool);
    tlog("  dev%d 动作完成 op=%d ok=%d", devIndex(sender) + 1, c->lastOp, (int)c->lastOk);
}

static void onParamReadDone(XObject* sender, XVarList* args)
{
    DrvCounters* c = &g_d[devIndex(sender)];
    if (!args) return;
    XVarList_start(args);
    ++c->paramReadCount;
    c->paramReadId = XVarList_arg(args, uint16_t);
    c->paramReadRaw = XVarList_arg(args, int32_t);
    c->paramReadOk = XVarList_arg(args, bool);
}

static void onParamWriteDone(XObject* sender, XVarList* args)
{
    DrvCounters* c = &g_d[devIndex(sender)];
    if (!args) return;
    XVarList_start(args);
    ++c->paramWriteCount;
    c->paramWriteId = XVarList_arg(args, uint16_t);
    c->paramWriteOk = XVarList_arg(args, bool);
}

static void onErrorOccurred(XObject* sender, XVarList* args)
{
    DrvCounters* c = &g_d[devIndex(sender)];
    if (!args) return;
    XVarList_start(args);
    ++c->errorCount;
    {
        const char* msg = XVarList_arg(args, const char*);
        if (msg) tlog("  dev%d 错误: %s", devIndex(sender) + 1, msg);
    }
}

int XVfdM512Test_runEx(const char* ptyOverride)
{
    XModbusTcpClient* client[2] = { NULL, NULL };
    XModbusDevice* mdev[2] = { NULL, NULL };
    XvfdHarness harness;
    XVfdM512* m1 = NULL;
    XVfdM512* m2 = NULL;
    const XVfdPointSpec* specs = NULL;
    const XVfdParamDesc* params = NULL;
    int specCount = 0;
    int paramCount = 0;
    int devId[2] = { -1, -1 };
    uint16_t port[2] = { 0, 0 };
    int i;
    (void)ptyOverride;

    memset(&harness, 0, sizeof(harness));
    memset(g_d, 0, sizeof(g_d));
    g_bootMs = XDateTime_currentMSecsSinceEpoch();
    printf("=== 511M/512M/513M 双站 XVfd 通道化设备层闭环测试（进程内 TCP 从站）===\n");
    fflush(stdout);
    XCoreApplication_create(0, NULL);

    /* ---------- 双站模拟器 + 客户端 ---------- */
    g_case = "topology";
    for (i = 0; i < 2; ++i) {
        XVariant* var;
        char name[32];
        CHECK(xvfdSlaveEmuCreate(&g_emu[i], XVFD_EMU_M512, &port[i]), "从站模拟器上线");
        client[i] = XModbusTcpClient_create();
        mdev[i] = (XModbusDevice*)client[i];
        CHECK(client[i] != NULL && port[i] != 0, "TCP 客户端创建");
        if (!client[i] || !port[i]) goto cleanup;
        XModbusClient_setTimeout((XModbusClient*)client[i], 500);
        XModbusClient_setNumberOfRetries((XModbusClient*)client[i], 2);
        var = XVariant_create_utf8_str("127.0.0.1");
        XModbusDevice_setConnectionParameter_ref(mdev[i],
            XModbusDevice_NetworkAddressParameter, var);
        XClassDelete((XClass*)var);
        var = XVariant_create_int((int)port[i]);
        XModbusDevice_setConnectionParameter_ref(mdev[i],
            XModbusDevice_NetworkPortParameter, var);
        XClassDelete((XClass*)var);
        snprintf(name, sizeof(name), "m512-u%d", i + 1);
        CHECK(XModbusDevice_connectDevice(mdev[i]), "connectDevice 返回 true");
        (void)name;
    }
    pump(300);

    /* ---------- 装配：一 Hub 两站 + 两组点 + 两台设备 ---------- */
    g_case = "assemble";
    CHECK(xvfdHarnessInit(&harness), "脚手架初始化");
    devId[0] = xvfdHarnessAddStation(&harness, (XModbusClient*)client[0], 1, "m512-u1");
    devId[1] = xvfdHarnessAddStation(&harness, (XModbusClient*)client[1], 2, "m512-u2");
    CHECK(devId[0] > 0 && devId[1] > 0, "双站设备注入 Hub（独立 server/client 对）");

    m1 = XVfdM512_create();
    m2 = XVfdM512_create();
    CHECK(m1 != NULL && m2 != NULL, "两台 XVfdM512 创建");
    if (!m1 || !m2) goto cleanup;
    g_dev[0] = (XVfdDevice*)m1;
    g_dev[1] = (XVfdDevice*)m2;
    g_devObj[0] = (XObject*)m1;
    g_devObj[1] = (XObject*)m2;

    CHECK(XVfdDevice_attach(g_dev[0], harness.m_group) &&
          XVfdDevice_attach(g_dev[1], harness.m_group), "两设备挂接同一通道组");
    specs = XVfdM512_pointSpecs(&specCount);
    params = XVfdM512_paramDescs(&paramCount);
    CHECK(xvfdHarnessMount(&harness, g_dev[0], devId[0], 1, specs, specCount),
          "站1 按点表建点并绑定");
    CHECK(xvfdHarnessMount(&harness, g_dev[1], devId[1], 2, specs, specCount),
          "站2 按点表建点并绑定（虚拟地址隔离）");
    CHECK(paramCount >= 12, "参数描述表规模（核心集）");
    CHECK(xvfdHarnessMountParam(&harness, g_dev[1], devId[1], 2, &params[3], 3),
          "站2 装配 P0.10 参数点（id=3）");
    CHECK(xvfdHarnessMountParam(&harness, g_dev[1], devId[1], 2, &params[11], 11),
          "站2 装配 P8.00 参数点（id=11）");
    CHECK(XVfdDevice_hasCapability(g_dev[0], XVfdCap_Jog), "能力位图：点动（511M 族特有）");
    CHECK(XVfdDevice_hasCapability(g_dev[1], XVfdCap_ParamAccess), "能力位图：参数通道");

    XObject_connect_2(g_devObj[0], XSignal(XVfdDevice_statusUpdated_signal), onStatusUpdated);
    XObject_connect_2(g_devObj[1], XSignal(XVfdDevice_statusUpdated_signal), onStatusUpdated);
    XObject_connect_2(g_devObj[0], XSignal(XVfdDevice_commandCompleted_signal), onCommandCompleted);
    XObject_connect_2(g_devObj[1], XSignal(XVfdDevice_commandCompleted_signal), onCommandCompleted);
    XObject_connect_2(g_devObj[1], XSignal(XVfdDevice_paramReadDone_signal), onParamReadDone);
    XObject_connect_2(g_devObj[1], XSignal(XVfdDevice_paramWriteDone_signal), onParamWriteDone);
    XObject_connect_2(g_devObj[0], XSignal(XVfdDevice_errorOccurred_signal), onErrorOccurred);
    XObject_connect_2(g_devObj[1], XSignal(XVfdDevice_errorOccurred_signal), onErrorOccurred);
    XVfdDevice_setStatusInterval(g_dev[0], 100);
    XVfdDevice_setStatusInterval(g_dev[1], 100);

    xvfdHarnessStart(&harness, 100);
    tlog("Hub 双站段轮询启动（100ms）");

    /* ---------- ① 双站快照 ---------- */
    g_case = "pollStatus";
    pump(2500);
    CHECK(g_d[0].statusCount > 0, "站1 出快照");
    CHECK(g_d[1].statusCount > 0, "站2 出快照");
    CHECK(XVfdDevice_status_const(g_dev[0])->m_runState == XVfdRunState_Stopped,
          "站1 初始 Stopped（3000H=3）");
    CHECK(XVfdDevice_status_const(g_dev[1])->m_runState == XVfdRunState_Stopped,
          "站2 初始 Stopped");

    /* ---------- ② 调速（Hz → 百分比 → 1000H） ---------- */
    g_case = "setFrequency";
    g_d[0].doneCount = 0;
    g_d[1].doneCount = 0;
    CHECK(XVfdDevice_setFrequency(g_dev[0], 25.0), "站1 发起 25.0Hz");
    pump(600);
    CHECK(XVfdDevice_setFrequency(g_dev[1], 10.0), "站2 发起 10.0Hz");
    pump(1500);
    CHECK(g_d[0].lastOk && g_d[0].lastOp == (int)XVfdOperation_SetFrequency, "站1 设定完成");
    CHECK(g_d[1].lastOk && g_d[1].lastOp == (int)XVfdOperation_SetFrequency, "站2 设定完成");
    pump(800);
    CHECK(XVfdDevice_status_const(g_dev[0])->m_outputFreqHz > 24.9 &&
          XVfdDevice_status_const(g_dev[0])->m_outputFreqHz < 25.1,
          "站1 输出频率 25.0Hz（0.01Hz × 最大频率百分比）");
    CHECK(XVfdDevice_status_const(g_dev[1])->m_outputFreqHz > 9.9 &&
          XVfdDevice_status_const(g_dev[1])->m_outputFreqHz < 10.1,
          "站2 输出频率 10.0Hz（站间隔离）");

    /* ---------- ③ 启停 ---------- */
    g_case = "startBoth";
    CHECK(XVfdDevice_start(g_dev[0], false), "站1 发起正转");
    pump(500);
    CHECK(XVfdDevice_start(g_dev[1], true), "站2 发起反转");
    pump(1500);
    CHECK(XVfdDevice_status_const(g_dev[0])->m_runState == XVfdRunState_RunningForward,
          "站1 正转（3000H=1）");
    CHECK(XVfdDevice_status_const(g_dev[1])->m_runState == XVfdRunState_RunningReverse,
          "站2 反转（3000H=2）");

    g_case = "stop";
    g_d[0].doneCount = 0;
    CHECK(XVfdDevice_stop(g_dev[0]), "站1 发起受控停机（2000H=6）");
    pump(1500);
    CHECK(g_d[0].lastOk, "站1 停机完成");
    CHECK(XVfdDevice_status_const(g_dev[0])->m_runState == XVfdRunState_Stopped,
          "站1 回到 Stopped");
    CHECK(XVfdDevice_status_const(g_dev[1])->m_runState == XVfdRunState_RunningReverse,
          "站2 仍在反转（站间隔离）");

    /* ---------- ④ 故障 ---------- */
    g_case = "fault";
    xvfdEmuM512InjectFault(&g_emu[0], 3);   /* E.oCdF 减速过电流 */
    pump(2500);
    CHECK(XVfdDevice_status_const(g_dev[0])->m_faulted, "站1 故障置位");
    CHECK(XVfdDevice_status_const(g_dev[0])->m_faultCode == 3, "站1 故障码 = 3");
    CHECK(XVfdM512_faultName(3) && strcmp(XVfdM512_faultName(3), "E.oCdF") == 0,
          "故障名映射 E.oCdF");
    CHECK(XVfdM512_faultText(3) && strcmp(XVfdM512_faultText(3), "减速过电流") == 0,
          "故障说明映射");
    CHECK(!XVfdDevice_status_const(g_dev[1])->m_faulted, "站2 无故障（隔离）");

    /* ---------- ⑤ 复位 ---------- */
    g_case = "resetFault";
    g_d[0].doneCount = 0;
    CHECK(XVfdDevice_resetFault(g_dev[0]), "站1 发起故障复位（2000H=7）");
    pump(2000);
    CHECK(g_d[0].lastOk, "站1 复位完成");
    CHECK(XVfdDevice_status_const(g_dev[0])->m_faultCode == 0, "站1 故障清除");

    /* ---------- ⑥ 参数通道 ---------- */
    g_case = "paramRead";
    g_d[1].paramReadOk = false;
    g_d[1].paramReadCount = 0;
    CHECK(XVfdDevice_readParam(g_dev[1], 3), "站2 发起参数读 P0.10（id=3）");
    pump(500);
    CHECK(g_d[1].paramReadOk && g_d[1].paramReadId == 3, "站2 参数读完成");
    CHECK(g_d[1].paramReadRaw == 5000, "P0.10 回读 raw=5000（50.00Hz）");

    g_case = "paramWrite";
    g_d[1].paramWriteOk = false;
    g_d[1].paramWriteCount = 0;
    CHECK(XVfdDevice_writeParam(g_dev[1], 11, 300), "站2 发起参数写 P8.00=300（3.00Hz）");
    pump(1500);
    CHECK(g_d[1].paramWriteOk && g_d[1].paramWriteId == 11, "站2 参数写完成");
    g_d[1].paramReadOk = false;
    CHECK(XVfdDevice_readParam(g_dev[1], 11), "站2 回读 P8.00");
    pump(1000);
    CHECK(g_d[1].paramReadOk && g_d[1].paramReadRaw == 300, "P8.00 回读 = 300（写入一致）");

    /* ---------- ⑦ 约束标注 ---------- */
    g_case = "paramAccess";
    {
        const XVfdParamDesc* pd = XVfdDevice_paramDesc_const(g_dev[1], 3);
        CHECK(pd && pd->m_access == (uint8_t)XVfdParamAccess_RunLocked,
              "P0.10 标注为运行禁改（★）");
        CHECK(XVfdDevice_paramCount(g_dev[1]) == paramCount, "参数表条目数一致");
    }

cleanup:
    if (g_dev[0]) XVfdDevice_detach(g_dev[0]);
    if (g_dev[1]) XVfdDevice_detach(g_dev[1]);
    if (m1) XClassDelete((XClass*)m1);
    if (m2) XClassDelete((XClass*)m2);
    if (harness.m_group || harness.m_hub) xvfdHarnessDestroy(&harness);
    for (i = 0; i < 2; ++i) {
        if (mdev[i]) XModbusDevice_disconnectDevice(mdev[i]);
        if (client[i]) XClassDelete((XClass*)client[i]);
        xvfdSlaveEmuDestroy(&g_emu[i]);
    }
    g_devObj[0] = NULL;
    g_devObj[1] = NULL;
    g_dev[0] = NULL;
    g_dev[1] = NULL;
    pump(50);

    printf("=== 结束：失败用例 %d ===\n", g_fail);
    fflush(stdout);
    return g_fail;
}

int XVfdM512Test_run(void)
{
    return XVfdM512Test_runEx(NULL);
}

void XVfdM512Test(XVariant* data)
{
    const int fails = XVfdM512Test_run();
    (void)data;
    printf("=== XVfdM512Test: %s（失败用例 %d）===\n",
           fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}
