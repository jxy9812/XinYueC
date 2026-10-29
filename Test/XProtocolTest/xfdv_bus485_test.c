/**
 * @file       xfdv_bus485_test.c
 * @brief      虚拟 RS485 总线（多从站）端到端测试。
 * @details    拓扑：Python 虚拟总线 hub（mbtest_bus485_hub.py，1 master pty +
 *             2 slave pty，总线广播/站号过滤/应答串行化/冲突检测）+ 两个独立
 *             从站进程（mbtest_m512_slave.py --unit 1/2，各占一个 slave pty）
 *             + 框架侧 XModbusRtuSerialClient（总线主站）+ XVfdModbusLink +
 *             两台 XVfdM512。这是**真多从站共享总线**形态（与单 pty 双站号
 *             模拟不同），复刻真实 RS485 的关键语义。
 *             用例：
 *               ①主站上线，两台设备挂同一总线，各自轮询出快照（站号过滤正确：
 *                 不匹配的站必须保持静默，不能串扰）；
 *               ②双站调速互不串扰（25.0Hz / 10.0Hz）；
 *               ③跨设备插队：站 2 持续轮询时，站 1 的动作帧先于站 2 后续轮询帧
 *                 到达从站（读 hub 帧流日志断言，真实总线上的顺序证据）；
 *               ④hub 无应答冲突记录（两站应答未交织）。
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
#include "XModbusRtuSerialClient.h"
#include "XModbusDevice.h"
#include "XVfdDevice.h"
#include "XVfdModbusLink.h"
#include "XVfdM512.h"
#include "XSerialPort.h"
#include "XVariant.h"
#include "XString.h"

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

static void tlog(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("[%8.3fs] ", (double)(XDateTime_currentMSecsSinceEpoch() - g_bootMs) / 1000.0);
    vprintf(fmt, ap); printf("\n"); fflush(stdout);
}

#define WAIT_FN(expr, timeoutMs) do { \
    const uint64_t _deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)(timeoutMs); \
    while (!(expr)) { \
        XCoreApplication_processEvents(XEventLoop_AllEvents); \
        const uint64_t _spin = XDateTime_currentMSecsSinceEpoch() + 2; \
        while (XDateTime_currentMSecsSinceEpoch() < _spin) { } \
        if (XDateTime_currentMSecsSinceEpoch() >= _deadline) break; \
    } \
} while (0)

static int g_statusCount[2];
static int g_doneCount[2];
static bool g_lastOk[2];

static void onStatusUpdated(XObject* sender, XVarList* args)
{
    XVfdDevice* dev = (XVfdDevice*)sender;
    int idx;
    (void)args;
    if (!dev) return;
    idx = (XVfdDevice_unitId(dev) == 1) ? 0 : 1;
    ++g_statusCount[idx];
}

static void onCommandCompleted(XObject* sender, XVarList* args)
{
    XVfdDevice* dev = (XVfdDevice*)sender;
    int idx;
    dev = (XVfdDevice*)sender;
    if (!dev) return;
    idx = (XVfdDevice_unitId(dev) == 1) ? 0 : 1;
    ++g_doneCount[idx];
    if (args) {
        XVarList_start(args);
        g_lastOk[idx] = XVarList_arg(args, bool);
    }
}

/** @brief 统计 hub 帧流日志总行数（用于增量扫描的基线）。 */
static int busFrameLines(void)
{
    char path[256];
    char line[512];
    FILE* f;
    int n = 0;
    snprintf(path, sizeof(path), "%s/bus_frames.log", BUS485_BASE);
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) ++n;
    fclose(f);
    return n;
}

/**
 * @brief 增量扫描 hub 帧流日志（仅 baseLine 之后的新增行）：
 *        找站 1 的写请求（M>S 01..06=动作）与站 2 的读请求（M>S 02..03=轮询）。
 * @details 主站发出的请求顺序即为插队的真实总线证据（hub 逐帧记录 M>S）。
 * @param baseLine 扫描起始行号（之前的行为增量）
 * @param outActionReq 动作请求在增量内的序号（-1=未找到）
 * @param outPollReq 站 2 轮询请求在增量内的序号（-1=未找到）
 * @param outConflicts 应答冲突计数
 * @return 扫描到的总行数
 */
static int scanBusFramesSince(int baseLine, int* outActionReq, int* outPollReq, int* outConflicts)
{
    char path[256];
    char line[512];
    FILE* f;
    int idx = 0;
    int total = 0;
    *outActionReq = -1;
    *outPollReq = -1;
    *outConflicts = 0;
    snprintf(path, sizeof(path), "%s/bus_frames.log", BUS485_BASE);
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        ++total;
        if (total <= baseLine) continue;   /* 增量之外 */
        if (strstr(line, "CONFLICT")) { ++(*outConflicts); continue; }
        /* "M>S <hex>"：主站发出的请求；hex = unit(1B) fc(1B) ... */
        if (strncmp(line, "M>S ", 4) == 0) {
            const char* hex = line + 4;
            if (strlen(hex) >= 4) {
                if (hex[0] == '0' && hex[1] == '1' && hex[2] == '0' && hex[3] == '6' &&
                    *outActionReq < 0) {
                    *outActionReq = idx;   /* 站 1 的写请求（动作） */
                } else if (hex[0] == '0' && hex[1] == '2' && hex[2] == '0' && hex[3] == '3' &&
                           *outPollReq < 0) {
                    *outPollReq = idx;     /* 站 2 的读请求（轮询） */
                }
            }
        }
        ++idx;
    }
    fclose(f);
    return total;
}

int XVfdBus485Test_runEx(const char* ptyOverride)
{
    char ptyPath[256] = { 0 };
    XVfdM512* d1;
    XVfdM512* d2;
    XVfdDevice* v1;
    XVfdDevice* v2;
    g_bootMs = XDateTime_currentMSecsSinceEpoch();

    printf("=== 虚拟 RS485 总线（多从站）端到端测试 ===\n"); fflush(stdout);
    XCoreApplication_create(0, NULL);

    /* ---------- 读 hub 分配的 master pty 路径 ---------- */
    g_case = "topology";
    if (ptyOverride && ptyOverride[0]) {
        snprintf(ptyPath, sizeof(ptyPath), "%s", ptyOverride);
    } else {
        char pathFile[256];
        snprintf(pathFile, sizeof(pathFile), "%s/bus_master_path", BUS485_BASE);
        FILE* f = fopen(pathFile, "r");
        if (f) {
            if (!fgets(ptyPath, sizeof(ptyPath), f)) ptyPath[0] = 0;
            fclose(f);
            ptyPath[strcspn(ptyPath, "\r\n")] = 0;
        }
    }
    CHECK(ptyPath[0] != 0, "读到虚拟总线 master pty 路径");
    if (!ptyPath[0]) return 1;
    tlog("虚拟总线 master: %s", ptyPath);

    /* ---------- 建链：一条总线 + 两台设备 ---------- */
    {
        XModbusRtuSerialClient* client = XModbusRtuSerialClient_create();
        XVfdModbusLink* link;
        CHECK(client != NULL, "RTU 客户端创建");
        XModbusClient_setTimeout((XModbusClient*)client, 800);
        XModbusClient_setNumberOfRetries((XModbusClient*)client, 2);
        link = XVfdModbusLink_create();
        CHECK(link != NULL, "Modbus 链路创建");
        CHECK(XVfdModbusLink_setClient(link, (XModbusClient*)client), "链路绑定 RTU 主站");
        XVfdLink_setTickMs(&link->m_base, 20);

        XModbusDevice* dev = (XModbusDevice*)client;
        XVariant* var = XString_toVariant(XString_create_utf8(ptyPath));
        if (var) XModbusDevice_setConnectionParameter_move(dev, XModbusDevice_SerialPortNameParameter, var);
        var = XVariant_create_int(9600);
        if (var) XModbusDevice_setConnectionParameter_move(dev, XModbusDevice_SerialBaudRateParameter, var);
        var = XVariant_create_int(XSerialPort_Data8);
        if (var) XModbusDevice_setConnectionParameter_move(dev, XModbusDevice_SerialDataBitsParameter, var);
        var = XVariant_create_int(XSerialPort_NoParity);
        if (var) XModbusDevice_setConnectionParameter_move(dev, XModbusDevice_SerialParityParameter, var);
        var = XVariant_create_int(XSerialPort_OneStop);
        if (var) XModbusDevice_setConnectionParameter_move(dev, XModbusDevice_SerialStopBitsParameter, var);

        g_case = "connect";
        CHECK(XModbusDevice_connectDevice(dev), "connectDevice 返回 true");
        WAIT_FN(XModbusDevice_state(dev) == XModbusDevice_ConnectedState, 3000);
        CHECK(XModbusDevice_state(dev) == XModbusDevice_ConnectedState, "设备进入 ConnectedState");

        d1 = XVfdM512_create();
        d2 = XVfdM512_create();
        v1 = &d1->m_base.m_base;
        v2 = &d2->m_base.m_base;
        CHECK(XVfdModbusBase_setLink(&d1->m_base, link, 1), "dev1 挂总线（unit=1）");
        CHECK(XVfdModbusBase_setLink(&d2->m_base, link, 2), "dev2 挂总线（unit=2，同一物理总线）");
        XObject_connect_2((XObject*)v1, XSignal(XVfdDevice_statusUpdated_signal), onStatusUpdated);
        XObject_connect_2((XObject*)v2, XSignal(XVfdDevice_statusUpdated_signal), onStatusUpdated);
        XObject_connect_2((XObject*)v1, XSignal(XVfdDevice_commandCompleted_signal), onCommandCompleted);
        XObject_connect_2((XObject*)v2, XSignal(XVfdDevice_commandCompleted_signal), onCommandCompleted);
        XVfdDevice_setPollInterval(v1, 200);
        XVfdDevice_setPollInterval(v2, 200);
        XVfdLink_startPolling(&link->m_base);

        /* ---------- ① 双站轮询（站号过滤正确） ---------- */
        g_case = "pollBoth";
        WAIT_FN(g_statusCount[0] > 0 && g_statusCount[1] > 0, 8000);
        CHECK(g_statusCount[0] > 0, "站 1 轮询有应答（真实总线上被寻址）");
        CHECK(g_statusCount[1] > 0, "站 2 轮询有应答（真实总线上被寻址）");
        CHECK(XVfdDevice_status_const(v1)->m_valid, "站 1 快照有效");
        CHECK(XVfdDevice_status_const(v2)->m_valid, "站 2 快照有效");

        /* ---------- ② 双站调速互不串扰 ---------- */
        g_case = "setFreqBoth";
        g_doneCount[0] = 0;
        g_doneCount[1] = 0;
        CHECK(XVfdDevice_setFrequency(v1, 25.0), "dev1 发起 25.0Hz");
        CHECK(XVfdDevice_setFrequency(v2, 10.0), "dev2 发起 10.0Hz");
        WAIT_FN(g_doneCount[0] > 0 && g_doneCount[1] > 0, 10000);
        CHECK(g_lastOk[0] && g_lastOk[1], "双站设定均成功");
        WAIT_FN(XVfdDevice_status_const(v1)->m_outputFreqHz > 24.9 &&
                XVfdDevice_status_const(v2)->m_outputFreqHz > 9.9, 10000);
        CHECK(XVfdDevice_status_const(v1)->m_outputFreqHz > 24.9 &&
              XVfdDevice_status_const(v1)->m_outputFreqHz < 25.1, "站 1 输出频率 25.0Hz");
        CHECK(XVfdDevice_status_const(v2)->m_outputFreqHz > 9.9 &&
              XVfdDevice_status_const(v2)->m_outputFreqHz < 10.1, "站 2 输出频率 10.0Hz（不串扰）");

        /* ---------- ③ 跨设备插队（真实总线帧序证据） ---------- */
        g_case = "priority";
        {
            int idxAction = -1;
            int idxPoll = -1;
            int conflicts = 0;
            int base = 0;
            uint64_t t0 = XDateTime_currentMSecsSinceEpoch();
            g_doneCount[0] = 0;
            /* 先让两站轮询跑一段，建立稳定的轮询流 */
            while (XDateTime_currentMSecsSinceEpoch() - t0 < 1500) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
            }
            base = busFrameLines();   /* 记录动作前的帧流基线 */
            CHECK(XVfdDevice_start(v1, false), "dev1 发起动作（High 优先）");
            WAIT_FN(g_doneCount[0] > 0, 8000);
            CHECK(g_lastOk[0], "dev1 动作完成");
            WAIT_FN(XVfdDevice_status_const(v1)->m_runState == XVfdRunState_RunningForward, 8000);
            CHECK(XVfdDevice_status_const(v1)->m_runState == XVfdRunState_RunningForward,
                  "站 1 已正转（动作在真实总线上生效）");
            scanBusFramesSince(base, &idxAction, &idxPoll, &conflicts);
            CHECK(idxAction >= 0, "hub 帧流日志捕获到站 1 的动作请求（M>S unit1 fc06）");
            CHECK(idxAction >= 0 && (idxPoll < 0 || idxAction < idxPoll),
                  "动作请求(High) 排在站 2 轮询请求(Normal) 之前——真实总线上插队生效");
            CHECK(conflicts == 0, "hub 未记录应答冲突（两站应答未交织）");
        }

        /* ---------- 收尾 ---------- */
        XVfdModbusBase_detach(&d1->m_base);
        XVfdModbusBase_detach(&d2->m_base);
        XModbusDevice_disconnectDevice(dev);
        XClassDelete((XClass*)d1);
        XClassDelete((XClass*)d2);
        XClassDelete((XClass*)link);
        XClassDelete((XClass*)client);
    }

    printf("=== 结束：失败用例 %d ===\n", g_fail);
    fflush(stdout);
    return g_fail;
}

int XVfdBus485Test_run(void)
{
    /* master pty 路径缺省读 BUS485_BASE/bus_master_path 文件（虚拟总线 hub 写入）。 */
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
