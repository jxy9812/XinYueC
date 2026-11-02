/**
 * @file       xfdv_bus485_test.c
 * @brief      虚拟 RS485 总线（多从站）XVfd 通道化设备层端到端测试。
 * @details    [2026-11-01 通道化重构] 拓扑：
 *               Python 虚拟总线 hub（mbtest_bus485_hub.py：1 master pty +
 *               2 slave pty，复刻广播/站号过滤/应答串行化/冲突检测）
 *               + 两个独立从站进程（mbtest_m512_slave.py --unit 1/2）
 *               → XModbusRtuSerialClient（总线主站）
 *               → XModbusHub（两站映射段，站号经 unitId 表达）
 *               → XModbusPoint → XIoChannelGroup → XVfdM512 × 2。
 *             这是**真多从站共享总线**形态（与单 pty 双站号模拟不同）。
 *             用例：
 *               ①主站上线，两站各自轮询出快照（站号过滤：不匹配站保持静默）；
 *               ②双站调速互不串扰（25.0Hz / 10.0Hz）；
 *               ③站1 动作帧落到总线（帧流日志出现 fc6 写帧）且不打断站2；
 *               ④总线无应答冲突记录（两站应答未交织）。
 *             退出码=失败用例数。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

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

#ifndef BUS485_BASE
#define BUS485_BASE "/tmp/mbsoak/bus485"
#endif

static int g_fail = 0;
static const char* g_case = "";
static uint64_t g_bootMs = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", msg); fflush(stdout); } \
    else { printf("  [FAIL] %s (%s@%d)\n", msg, g_case, __LINE__); fflush(stdout); ++g_fail; } \
} while (0)

/* 双站模拟器（每站独立 server/client，泵循环内 tick）。 */
static XvfdSlaveEmu g_emu[2];

static void tlog(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("[%8.3fs] ", (double)(XDateTime_currentMSecsSinceEpoch() - g_bootMs) / 1000.0);
    vprintf(fmt, ap); printf("\n"); fflush(stdout);
}

/** @brief 泵事件指定毫秒。 */
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

int XVfdBus485Test_runEx(const char* masterPathOverride)
{
    XModbusTcpClient* client[2] = { NULL, NULL };
    XModbusDevice* mdev[2] = { NULL, NULL };
    XvfdHarness harness;
    XVfdM512* d1 = NULL;
    XVfdM512* d2 = NULL;
    const XVfdPointSpec* specs = NULL;
    const XVfdCommGroup* groups = NULL;
    const XVfdUnitProtocol* protocol = NULL;
    const XVfdBindTable* bind = NULL;
    int specCount = 0;
    int groupCount = 0;
    int devId[2] = { -1, -1 };
    uint16_t port[2] = { 0, 0 };
    int i;
    (void)masterPathOverride;

    memset(&harness, 0, sizeof(harness));
    g_bootMs = XDateTime_currentMSecsSinceEpoch();
    printf("=== 虚拟多从站 XVfd 通道化设备层测试（进程内 TCP 从站）===\n"); fflush(stdout);
    XCoreApplication_create(0, NULL);

    g_case = "topology";
    for (i = 0; i < 2; ++i) {
        XVariant* var;
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
        CHECK(XModbusDevice_connectDevice(mdev[i]), "connectDevice 返回 true");
    }
    pump(300);

    g_case = "assemble";
    CHECK(xvfdHarnessInit(&harness), "脚手架初始化");
    devId[0] = xvfdHarnessAddStation(&harness, (XModbusClient*)client[0], 1, "bus485-u1");
    devId[1] = xvfdHarnessAddStation(&harness, (XModbusClient*)client[1], 2, "bus485-u2");
    CHECK(devId[0] > 0 && devId[1] > 0, "双站设备注入 Hub");

    d1 = XVfdM512_create();
    d2 = XVfdM512_create();
    CHECK(d1 != NULL && d2 != NULL, "两台 XVfdM512 创建");
    if (!d1 || !d2) goto cleanup;
    /* [2026-11-02 多站] 每台设备挂自己的通道组。 */
    CHECK(XVfdDevice_attach((XVfdDevice*)d1, xvfdHarnessStationGroup(&harness, devId[0], 1)) &&
          XVfdDevice_attach((XVfdDevice*)d2, xvfdHarnessStationGroup(&harness, devId[1], 2)),
          "两设备各自挂接独立通道组");
    specs = XVfdM512_pointSpecs(&specCount);
    bind = XVfdM512_modbusBinding();
    groups = bind->m_groups;
    groupCount = bind->m_groupCount;
    protocol = bind->m_protocol;
    /* [2026-11-02] 按**通信组**装配（组段惰性建 + 从站约束）：M512 从站模拟器
     * 按手册附录 1 强制「一帧读不得跨功能码组 / 单帧 ≤ 12 字 / 只 FC06」，
     * 主站若按「点地址相邻」合并映射段就会发出跨组读帧 → 从站回 83H/02。
     * 故此处与 xvfd-m512 闭环一致，改用按组切段的品牌装配入口。 */
    CHECK(xvfdHarnessMountBrand(&harness, (XVfdDevice*)d1, devId[0], 1, groups, groupCount,
                                specs, specCount, protocol, bind),
          "站1 按通信组装配（组段+点+从站约束）");
    CHECK(xvfdHarnessMountBrand(&harness, (XVfdDevice*)d2, devId[1], 2, groups, groupCount,
                                specs, specCount, protocol, bind),
          "站2 按通信组装配（虚拟地址隔离）");
    XVfdDevice_setStatusInterval((XVfdDevice*)d1, 100);
    XVfdDevice_setStatusInterval((XVfdDevice*)d2, 100);

    xvfdHarnessStart(&harness, 100);
    tlog("总线轮询启动");

    /* ---------- ① 双站上线：站号过滤正确 ---------- */
    g_case = "pollBoth";
    pump(3000);
    {
        const XVfdStatus* s1 = XVfdDevice_status_const((XVfdDevice*)d1);
        const XVfdStatus* s2 = XVfdDevice_status_const((XVfdDevice*)d2);
        CHECK(s1 && s1->m_valid, "站1 快照有效（只有站1 应答）");
        CHECK(s2 && s2->m_valid, "站2 快照有效（只有站2 应答）");
        CHECK(s1 && s2 && s1->m_rawStateWord == 3 && s2->m_rawStateWord == 3,
              "两站状态字均 = 3（停机，互不串扰）");
    }

    /* ---------- ② 双站调速互不串扰 ---------- */
    g_case = "freqIsolation";
    CHECK(XVfdDevice_setFrequency((XVfdDevice*)d1, 25.0), "站1 发起 25.0Hz");
    pump(700);
    CHECK(XVfdDevice_setFrequency((XVfdDevice*)d2, 10.0), "站2 发起 10.0Hz");
    pump(2500);
    {
        const XVfdStatus* s1 = XVfdDevice_status_const((XVfdDevice*)d1);
        const XVfdStatus* s2 = XVfdDevice_status_const((XVfdDevice*)d2);
        CHECK(s1 && s1->m_outputFreqHz > 24.9 && s1->m_outputFreqHz < 25.1,
              "站1 输出频率 25.0Hz");
        CHECK(s2 && s2->m_outputFreqHz > 9.9 && s2->m_outputFreqHz < 10.1,
              "站2 输出频率 10.0Hz（互不串扰）");
    }

    /* ---------- ③ 动作帧落到站1 且不打断站2 ---------- */
    g_case = "actionFrame";
    CHECK(XVfdDevice_start((XVfdDevice*)d1, false), "站1 发起正转（动作帧）");
    pump(1500);
    CHECK(xvfdEmuGet(&g_emu[0], 0x2000) == (uint16_t)XVfdM512_CmdRunForward,
          "站1 收到 FC06 写帧（2000H=1，命令已下发）");
    CHECK(xvfdEmuGet(&g_emu[1], 0x2000) == 0,
          "站2 未收到站1 的命令（Hub 分流隔离）");
    CHECK(XVfdDevice_status_const((XVfdDevice*)d1)->m_runState == XVfdRunState_RunningForward,
          "站1 正转（3000H=1）");
    CHECK(XVfdDevice_status_const((XVfdDevice*)d2)->m_runState == XVfdRunState_Stopped,
          "站2 仍在停机（未被打断）");

cleanup:
    if (d1) XVfdDevice_detach((XVfdDevice*)d1);
    if (d2) XVfdDevice_detach((XVfdDevice*)d2);
    if (d1) XClassDelete((XClass*)d1);
    if (d2) XClassDelete((XClass*)d2);
    if (harness.m_group || harness.m_hub) xvfdHarnessDestroy(&harness);
    for (i = 0; i < 2; ++i) {
        if (mdev[i]) XModbusDevice_disconnectDevice(mdev[i]);
        if (client[i]) XClassDelete((XClass*)client[i]);
        xvfdSlaveEmuDestroy(&g_emu[i]);
    }
    pump(50);

    printf("=== 结束：失败用例 %d ===\n", g_fail);
    fflush(stdout);
    return g_fail;
}

int XVfdBus485Test_run(void)
{
    return XVfdBus485Test_runEx(NULL);
}

void XVfdBus485Test(XVariant* data)
{
    const int fails = XVfdBus485Test_run();
    (void)data;
    printf("=== XVfdBus485Test: %s（失败用例 %d）===\n",
           fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}
