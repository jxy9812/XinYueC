/**
 * @file       xfdv_atv61_test.c
 * @brief      ATV61 变频器 XVfd 抽象层 Modbus RTU 闭环测试（端到端）。
 * @details    拓扑：Python 模拟从站（mbtest_atv61_slave.py，os.openpty 虚拟串口）
 *             + 框架侧 XModbusRtuSerialClient（总线主站）+ XVfdModbusLink 链路
 *             + XVfdAtv61 品牌子类。迁移自 xmodbus_atv61_test.c（旧驱动已删除），
 *             读状态用例改走链路轮询计划表（验证新架构的统一排拍）。
 *             用例：
 *               ①串口上线（38400/8E1，pty 从端）→ Connected；
 *               ②轮询状态：计划表排拍 → ETA/输出频率/电流/转矩/热状态快照回填；
 *               ③调速：写 50.0 Hz → 轮询回读输出频率 50.0 Hz（0.1 Hz 定点核对）；
 *               ④启动：CiA402 状态机推进 → ETA bit2 置位（运行中）；
 *               ⑤停止：受控停机 → 轮询确认 ETA 回到状态 4（Switched on）；
 *               ⑥报警：注入故障 OCF(9) → 轮询读到 ETA bit3 + LFT=9 + 故障名 OCF；
 *               ⑦故障复位 → 故障位清零、状态回到可启动。
 *             全部用例走统一事件循环（单线程，与库同款）；退出码=失败用例数。
 *
 *             ⚠ 信号槽约定：槽第一参语义由连接 API 决定——本程序一律 connect_2
 *             （第一参=真 sender）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XDateTime.h"
#include "XModbusRtuSerialClient.h"
#include "XModbusDevice.h"
#include "XVfdDevice.h"
#include "XVfdDevice_Protected.h"
#include "XVfdModbusLink.h"
#include "XVfdAtv61.h"
#include "XSerialPort.h"
#include "XVariant.h"
#include "XString.h"

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

typedef bool (*Pred)(void* ctx);

/** @brief 事件循环内等待谓词成立（默认 5s 超时）。 */
static bool waitUntil(Pred pred, void* ctx, int timeoutMs)
{
    const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)timeoutMs;
    while (!pred(ctx)) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        const uint64_t spin = XDateTime_currentMSecsSinceEpoch() + 2;
        while (XDateTime_currentMSecsSinceEpoch() < spin) { /* 2ms 节拍自旋 */ }
        if (XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    }
    return true;
}

/* ==================== 设备信号计数 ==================== */
typedef struct {
    int statusCount;
    int doneCount;
    int lastOp;
    bool lastOk;
    int errorCount;
    char lastError[128];
} DriverCounters;

static DriverCounters g_drv;

static void onStatusUpdated(XObject* sender, XVarList* args)
{
    (void)sender;
    ++g_drv.statusCount;
    if (!args) return;
    XVarList_start(args);
    const XVfdStatus* st = XVarList_arg(args, const XVfdStatus*);
    if (st) {
        tlog("  状态更新: raw=0x%04X state=%d f=%.1fHz I=%.0f T=%.1f%% fault=%u",
             st->m_rawStateWord, (int)st->m_runState, st->m_outputFreqHz,
             st->m_outputCurrentA, st->m_torquePercent, st->m_faultCode);
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
    const char* msg = XVarList_arg(args, const char*);
    if (msg) {
        snprintf(g_drv.lastError, sizeof(g_drv.lastError), "%s", msg);
        tlog("  设备错误: %s", msg);
    }
}

static bool predDone(void* ctx) { (void)ctx; return g_drv.doneCount > 0; }
static bool predConnected(void* ctx)
{
    XModbusDevice* dev = (XModbusDevice*)ctx;
    return XModbusDevice_state(dev) == XModbusDevice_ConnectedState;
}
/** @brief 状态更新计数超过 ctx（intptr_t 基线）谓词 */
static bool predStatusGt(void* ctx) { return g_drv.statusCount > (int)(intptr_t)ctx; }

/** @brief 写文件（故障注入） */
static void touchFile(const char* path, const char* content)
{
    FILE* f = fopen(path, "w");
    if (!f) return;
    if (content) fputs(content, f);
    fclose(f);
}

int main(int argc, char* argv[])
{
    char ptyPath[256] = { 0 };
    g_bootMs = XDateTime_currentMSecsSinceEpoch();

    printf("=== ATV61 变频器 XVfd 抽象层闭环测试 ===\n"); fflush(stdout);
    XCoreApplication_create(0, NULL);

    /* ---------- 读从端 pty 路径 ---------- */
    g_case = "topology";
    if (argc > 1) {
        snprintf(ptyPath, sizeof(ptyPath), "%s", argv[1]);
    } else {
        char pathFile[256];
        snprintf(pathFile, sizeof(pathFile), "%s/pty_path", ATV61_BASE);
        FILE* f = fopen(pathFile, "r");
        if (f) {
            if (!fgets(ptyPath, sizeof(ptyPath), f)) ptyPath[0] = 0;
            fclose(f);
            ptyPath[strcspn(ptyPath, "\r\n")] = 0;
        }
    }
    CHECK(ptyPath[0] != 0, "读到从站 pty 路径");
    if (!ptyPath[0]) return 1;
    tlog("从站串口: %s", ptyPath);

    /* ---------- ① 建链：RTU 主站 + Modbus 链路 ---------- */
    XModbusRtuSerialClient* client = XModbusRtuSerialClient_create();
    CHECK(client != NULL, "RTU 客户端创建");
    XModbusClient_setTimeout((XModbusClient*)client, 800);
    XModbusClient_setNumberOfRetries((XModbusClient*)client, 2);

    XVfdModbusLink* link = XVfdModbusLink_create();
    CHECK(link != NULL, "Modbus 链路创建");
    CHECK(XVfdModbusLink_setClient(link, (XModbusClient*)client), "链路绑定 RTU 主站");
    XVfdLink_setTickMs(&link->m_base, 20);

    XModbusDevice* dev = (XModbusDevice*)client;
    XVariant* var = XString_toVariant(XString_create_utf8(ptyPath));
    if (var) {
        XModbusDevice_setConnectionParameter_move(dev,
            XModbusDevice_SerialPortNameParameter, var);
    }
    var = XVariant_create_int(38400);
    if (var) {
        XModbusDevice_setConnectionParameter_move(dev,
            XModbusDevice_SerialBaudRateParameter, var);
    }
    var = XVariant_create_int(XSerialPort_Data8);
    if (var) {
        XModbusDevice_setConnectionParameter_move(dev,
            XModbusDevice_SerialDataBitsParameter, var);
    }
    var = XVariant_create_int(XSerialPort_EvenParity);
    if (var) {
        XModbusDevice_setConnectionParameter_move(dev,
            XModbusDevice_SerialParityParameter, var);
    }
    var = XVariant_create_int(XSerialPort_OneStop);
    if (var) {
        XModbusDevice_setConnectionParameter_move(dev,
            XModbusDevice_SerialStopBitsParameter, var);
    }

    g_case = "connect";
    bool connected = XModbusDevice_connectDevice(dev);
    CHECK(connected, "connectDevice 返回 true");
    CHECK(waitUntil(predConnected, dev, 3000), "设备进入 ConnectedState");

    /* ---------- 建设备（品牌子类） ---------- */
    XVfdAtv61* drive = XVfdAtv61_create();
    CHECK(drive != NULL, "XVfdAtv61 创建");
    CHECK(XVfdDevice_hasCapability(&drive->m_base.m_base, XVfdCap_Reverse), "能力位图：反转");
    CHECK(XVfdDevice_caps(&drive->m_base.m_base) != 0, "能力位图非空");
    CHECK(XVfdModbusBase_setLink(&drive->m_base, link, 1), "设备挂链路（unit=1，登记轮询计划）");
    XObject_connect_2((XObject*)drive, XSignal(XVfdDevice_statusUpdated_signal), onStatusUpdated);
    XObject_connect_2((XObject*)drive, XSignal(XVfdDevice_commandCompleted_signal), onCommandCompleted);
    XObject_connect_2((XObject*)drive, XSignal(XVfdDevice_errorOccurred_signal), onErrorOccurred);
    XVfdDevice_setPollInterval(&drive->m_base.m_base, 150);

    /* ---------- ② 轮询状态 ---------- */
    g_case = "pollStatus";
    g_drv.statusCount = 0;
    CHECK(waitUntil(predStatusGt, (void*)(intptr_t)0, 5000), "轮询收到状态快照（计划表排拍→FC03 收发闭环）");
    {
        const XVfdStatus* st = XVfdDevice_status_const(&drive->m_base.m_base);
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
    CHECK(XVfdDevice_setFrequency(&drive->m_base.m_base, 50.0), "发起写频率给定 50.0 Hz");
    CHECK(waitUntil(predDone, NULL, 4000), "写给定完成");
    CHECK(g_drv.lastOk && g_drv.lastOp == (int)XVfdOperation_SetFrequency, "写频率给定成功");
    {
        /* 三组轮转需数拍才回到主状态组：按值等待而非按次数等待 */
        const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 5000;
        const XVfdStatus* st = XVfdDevice_status_const(&drive->m_base.m_base);
        while (!(st->m_outputFreqHz > 49.9 && st->m_outputFreqHz < 50.1)) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            const uint64_t spin = XDateTime_currentMSecsSinceEpoch() + 2;
            while (XDateTime_currentMSecsSinceEpoch() < spin) { }
            if (XDateTime_currentMSecsSinceEpoch() >= deadline) break;
        }
        CHECK(st && st->m_outputFreqHz > 49.9 && st->m_outputFreqHz < 50.1,
              "输出频率回读 = 50.0 Hz（0.1Hz 定点）");
    }

    /* ---------- ④ 启动 ---------- */
    g_case = "start";
    g_drv.doneCount = 0;
    CHECK(XVfdDevice_start(&drive->m_base.m_base, false), "发起启动");
    CHECK(waitUntil(predDone, NULL, 8000), "启动序列完成");
    CHECK(g_drv.lastOk && g_drv.lastOp == (int)XVfdOperation_Start, "启动成功");
    {
        const XVfdStatus* st = XVfdDevice_status_const(&drive->m_base.m_base);
        CHECK(st && st->m_running, "ETA bit2 置位（Operation enabled）");
        CHECK(st && (st->m_rawStateWord & XVfdAtv61_EtaMask) == XVfdAtv61_EtaStateRunning,
              "ETA 状态 = 5（Operation enabled）");
        CHECK(st && st->m_remote, "ETA bit9 置位（命令来自总线）");
    }

    /* ---------- ⑤ 停止 ---------- */
    g_case = "stop";
    g_drv.doneCount = 0;
    CHECK(XVfdDevice_stop(&drive->m_base.m_base), "发起受控停机");
    CHECK(waitUntil(predDone, NULL, 4000), "停机完成");
    CHECK(g_drv.lastOk, "停机成功");
    {
        /* 写应答收口≠状态已刷新：按值等轮询把 main 组读进来 */
        const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 5000;
        const XVfdStatus* st = XVfdDevice_status_const(&drive->m_base.m_base);
        while (st->m_running ||
               (st->m_rawStateWord & XVfdAtv61_EtaMask) != XVfdAtv61_EtaStateSwitched) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            const uint64_t spin = XDateTime_currentMSecsSinceEpoch() + 2;
            while (XDateTime_currentMSecsSinceEpoch() < spin) { }
            if (XDateTime_currentMSecsSinceEpoch() >= deadline) break;
        }
        CHECK(st && !st->m_running, "停机后 ETA bit2 清零");
        CHECK(st && (st->m_rawStateWord & XVfdAtv61_EtaMask) == XVfdAtv61_EtaStateSwitched,
              "停机后 ETA 状态 = 4（Switched on）");
    }

    /* ---------- ⑥ 报警 ---------- */
    g_case = "fault";
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/fault", ATV61_BASE);
        touchFile(path, "9");            /* OCF 过流 */
    }
    {
        /* 等轮询转到 LFT 组读到故障码（三组轮转，放宽超时） */
        const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 8000;
        const XVfdStatus* st = XVfdDevice_status_const(&drive->m_base.m_base);
        while (!(st->m_faulted && st->m_faultCode == 9)) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            const uint64_t spin = XDateTime_currentMSecsSinceEpoch() + 2;
            while (XDateTime_currentMSecsSinceEpoch() < spin) { }
            if (XDateTime_currentMSecsSinceEpoch() >= deadline) break;
        }
        CHECK(st && st->m_faulted, "ETA bit3 置位（故障）");
        CHECK(st && st->m_faultCode == 9, "LFT 故障码 = 9");
        CHECK(XVfdAtv61_faultName(9) != NULL &&
              strcmp(XVfdAtv61_faultName(9), "OCF") == 0, "故障名映射 OCF");
        CHECK(XVfdAtv61_faultText(9) != NULL, "故障说明可查");
    }

    /* ---------- ⑦ 故障复位 ---------- */
    g_case = "resetFault";
    g_drv.doneCount = 0;
    CHECK(XVfdDevice_resetFault(&drive->m_base.m_base), "发起故障复位");
    CHECK(waitUntil(predDone, NULL, 4000), "复位动作完成");
    CHECK(g_drv.lastOk, "故障复位成功");
    {
        const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 8000;
        const XVfdStatus* st = XVfdDevice_status_const(&drive->m_base.m_base);
        while (st->m_faulted || st->m_faultCode != 0) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            const uint64_t spin = XDateTime_currentMSecsSinceEpoch() + 2;
            while (XDateTime_currentMSecsSinceEpoch() < spin) { }
            if (XDateTime_currentMSecsSinceEpoch() >= deadline) break;
        }
        CHECK(!st->m_faulted, "复位后 ETA bit3 清零");
        CHECK(st->m_faultCode == 0, "复位后 LFT = 0");
    }

    /* ---------- 收尾 ---------- */
    XModbusDevice_disconnectDevice(dev);
    XVfdModbusBase_detach(&drive->m_base);
    XClassDelete((XClass*)drive);
    XClassDelete((XClass*)link);
    XClassDelete((XClass*)client);

    printf("=== 结束：失败用例 %d ===\n", g_fail);
    fflush(stdout);
    return g_fail;
}
