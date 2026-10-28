/**
 * @file       xfdv_m512_test.c
 * @brief      511M/512M/513M 变频器 XVfd 抽象层闭环测试（双站混合总线端到端）。
 * @details    拓扑：Python 模拟从站（mbtest_m512_slave.py，os.openpty 虚拟串口，
 *             unit1/unit2 双站独立状态机）+ XModbusRtuSerialClient（总线主站）
 *             + XVfdModbusLink + 两台 XVfdM512 挂同一条链路——验证共享 485 总线
 *             仲裁与按站隔离。用例：
 *               ①建链上线 → 双站轮询各自出快照（Stopped）；
 *               ②双站调速互不串扰：dev1=25.0Hz / dev2=10.0Hz（经 P0.10 百分比
 *                 换算，1000H ±10000 口径），轮询回读各自输出频率；
 *               ③双站启动隔离：dev1 正转 / dev2 反转 → 3000H 各自 1/2；
 *               ④dev1 受控停机 → 3000H=3；
 *               ⑤故障注入 u1（8000H=3 减速过电流）→ dev1 故障 + 名称映射，
 *                 dev2 无故障（隔离）；
 *               ⑥dev1 故障复位 → 2000H=7 → 清除；
 *               ⑦dev2 参数读 P0.10（raw=5000）+ 参数写 P8.00=300 回读一致；
 *               ⑧从站静默：mute 站 2 → 链路 slaveLost(2)（连续超时退避），
 *                 站 1 轮询不受牵连；解除静默 → slaveAlive(2) + 轮询恢复。
 *             全部用例走统一事件循环；退出码=失败用例数。
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
#include "XVfdDevice_Protected.h"
#include "XVfdModbusLink.h"
#include "XVfdM512.h"
#include "XSerialPort.h"
#include "XVariant.h"
#include "XString.h"

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

static void tlog(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("[%8.3fs] ", (double)(XDateTime_currentMSecsSinceEpoch() - g_bootMs) / 1000.0);
    vprintf(fmt, ap); printf("\n"); fflush(stdout);
}

/** @brief 事件循环内自旋等待谓词成立。 */
#define WAIT_FN(expr, timeoutMs) do { \
    const uint64_t _deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)(timeoutMs); \
    while (!(expr)) { \
        XCoreApplication_processEvents(XEventLoop_AllEvents); \
        const uint64_t _spin = XDateTime_currentMSecsSinceEpoch() + 2; \
        while (XDateTime_currentMSecsSinceEpoch() < _spin) { } \
        if (XDateTime_currentMSecsSinceEpoch() >= _deadline) break; \
    } \
} while (0)

/* ==================== 信号计数 ==================== */
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
} DevCounters;

static DevCounters g_d[2];          /* [0]=unit1 [1]=unit2 */
static uint64_t g_lastStatusMs[2] = { 0, 0 };
static uint64_t g_statusGapMs[2] = { 0, 0 };
static int g_lostUnit = -1;         /* 链路 slaveLost 最近站号 */
static int g_aliveUnit = -1;        /* 链路 slaveAlive 最近站号 */

static void onStatusUpdated(XObject* sender, XVarList* args)
{
    XVfdDevice* dev;
    int idx;
    (void)args;
    dev = (XVfdDevice*)sender; /* connect_2：第一参=真 sender */
    if (!dev) return;
    idx = (XVfdDevice_unitId(dev) == 1) ? 0 : 1;
    {
        uint64_t now = XDateTime_currentMSecsSinceEpoch();
        if (g_lastStatusMs[idx] != 0) {
            uint64_t gap = now - g_lastStatusMs[idx];
            if (g_statusGapMs[idx] == 0 || gap < g_statusGapMs[idx]) g_statusGapMs[idx] = gap;
        }
        g_lastStatusMs[idx] = now;
    }
    ++g_d[idx].statusCount;
    if ((g_d[idx].statusCount % 8) == 1) {
        const XVfdStatus* st = XVfdDevice_status_const(dev);
        tlog("  u%d 快照: raw=0x%04X state=%d f=%.2fHz I=%.2fA P=%.1fkW fault=%u",
             idx + 1, st->m_rawStateWord, (int)st->m_runState, st->m_outputFreqHz,
             st->m_outputCurrentA, st->m_outputPowerKw, st->m_faultCode);
    }
}

static void onCommandCompleted(XObject* sender, XVarList* args)
{
    XVfdDevice* dev;
    int idx;
    dev = (XVfdDevice*)sender; /* connect_2：第一参=真 sender */
    if (!dev) return;
    idx = (XVfdDevice_unitId(dev) == 1) ? 0 : 1;
    ++g_d[idx].doneCount;
    if (args) {
        XVarList_start(args);
        g_d[idx].lastOp = (int)XVarList_arg(args, XVfdOperation);
        g_d[idx].lastOk = XVarList_arg(args, bool);
    }
    tlog("  u%d 动作完成: op=%d ok=%d", idx + 1, g_d[idx].lastOp, (int)g_d[idx].lastOk);
}

static void onParamReadDone(XObject* sender, XVarList* args)
{
    XVfdDevice* dev;
    int idx;
    dev = (XVfdDevice*)sender; /* connect_2：第一参=真 sender */
    if (!dev) return;
    idx = (XVfdDevice_unitId(dev) == 1) ? 0 : 1;
    ++g_d[idx].paramReadCount;
    if (args) {
        XVarList_start(args);
        g_d[idx].paramReadId = XVarList_arg(args, uint16_t);
        g_d[idx].paramReadRaw = XVarList_arg(args, int32_t);
        g_d[idx].paramReadOk = XVarList_arg(args, bool);
    }
    tlog("  u%d 参数读: id=%u raw=%d ok=%d", idx + 1,
         g_d[idx].paramReadId, g_d[idx].paramReadRaw, (int)g_d[idx].paramReadOk);
}

static void onParamWriteDone(XObject* sender, XVarList* args)
{
    XVfdDevice* dev;
    int idx;
    dev = (XVfdDevice*)sender; /* connect_2：第一参=真 sender */
    if (!dev) return;
    idx = (XVfdDevice_unitId(dev) == 1) ? 0 : 1;
    ++g_d[idx].paramWriteCount;
    if (args) {
        XVarList_start(args);
        g_d[idx].paramWriteId = XVarList_arg(args, uint16_t);
        g_d[idx].paramWriteOk = XVarList_arg(args, bool);
    }
    tlog("  u%d 参数写: id=%u ok=%d", idx + 1, g_d[idx].paramWriteId, (int)g_d[idx].paramWriteOk);
}

static void onSlaveLost(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        XVarList_start(args);
        g_lostUnit = (int)XVarList_arg(args, int);
        tlog("  链路事件: slaveLost(unit=%d)", g_lostUnit);
    }
}

static void onSlaveAlive(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        XVarList_start(args);
        g_aliveUnit = (int)XVarList_arg(args, int);
        tlog("  链路事件: slaveAlive(unit=%d)", g_aliveUnit);
    }
}

/** @brief 写控制文件（故障注入/静默）。 */
static void writeFile(const char* path, const char* content)
{
    FILE* f = fopen(path, "w");
    if (!f) return;
    if (content) fputs(content, f);
    fclose(f);
}

int main(int argc, char* argv[])
{
    char ptyPath[256] = { 0 };
    XVfdM512* d1;
    XVfdM512* d2;
    XVfdDevice* v1;
    XVfdDevice* v2;
    g_bootMs = XDateTime_currentMSecsSinceEpoch();

    printf("=== 511M/512M/513M XVfd 抽象层闭环测试（双站混合总线） ===\n"); fflush(stdout);
    XCoreApplication_create(0, NULL);

    /* ---------- 读从端 pty 路径 ---------- */
    g_case = "topology";
    if (argc > 1) {
        snprintf(ptyPath, sizeof(ptyPath), "%s", argv[1]);
    } else {
        char pathFile[256];
        snprintf(pathFile, sizeof(pathFile), "%s/pty_path", M512_BASE);
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

    /* ---------- ① 建链：一条 485 总线 + 两台设备 ---------- */
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
        CHECK(d1 && d2, "两台 XVfdM512 创建");
        CHECK(XVfdDevice_hasCapability(v2, XVfdCap_Jog), "能力位图：点动（511M 族特有）");
        CHECK(XVfdModbusBase_setLink(&d1->m_base, link, 1), "dev1 挂链路（unit=1）");
        CHECK(XVfdModbusBase_setLink(&d2->m_base, link, 2), "dev2 挂链路（unit=2，同一条 485 总线）");
        XObject_connect_2((XObject*)v1, XSignal(XVfdDevice_statusUpdated_signal), onStatusUpdated);
        XObject_connect_2((XObject*)v2, XSignal(XVfdDevice_statusUpdated_signal), onStatusUpdated);
        XObject_connect_2((XObject*)v1, XSignal(XVfdDevice_commandCompleted_signal), onCommandCompleted);
        XObject_connect_2((XObject*)v2, XSignal(XVfdDevice_commandCompleted_signal), onCommandCompleted);
        XObject_connect_2((XObject*)v1, XSignal(XVfdDevice_paramReadDone_signal), onParamReadDone);
        XObject_connect_2((XObject*)v2, XSignal(XVfdDevice_paramReadDone_signal), onParamReadDone);
        XObject_connect_2((XObject*)v1, XSignal(XVfdDevice_paramWriteDone_signal), onParamWriteDone);
        XObject_connect_2((XObject*)v2, XSignal(XVfdDevice_paramWriteDone_signal), onParamWriteDone);
        XObject_connect_2((XObject*)link, XSignal(XVfdLink_slaveLost_signal), onSlaveLost);
        XObject_connect_2((XObject*)link, XSignal(XVfdLink_slaveAlive_signal), onSlaveAlive);
        XVfdDevice_setPollInterval(v1, 150);
        XVfdDevice_setPollInterval(v2, 150);
        XVfdLink_startPolling(&link->m_base);

        /* ---------- ② 双站轮询（计划表统一排拍） ---------- */
        g_case = "pollBoth";
        WAIT_FN(g_d[0].statusCount > 0 && g_d[1].statusCount > 0, 6000);
        CHECK(g_d[0].statusCount > 0 && g_d[1].statusCount > 0, "双站轮询均出快照（同链路排拍）");
        CHECK(XVfdDevice_status_const(v1)->m_runState == XVfdRunState_Stopped &&
              XVfdDevice_status_const(v2)->m_runState == XVfdRunState_Stopped,
              "双站初始状态 = Stopped");

        /* ---- 实测：链路在途深度（跨设备动作排队长度）+ 轮询实际周期 ---- */
        {
            uint64_t t0 = XDateTime_currentMSecsSinceEpoch();
            int c0 = g_d[0].statusCount + g_d[1].statusCount;
            int peak = 0;
            while (XDateTime_currentMSecsSinceEpoch() - t0 < 2000) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                if (XVfdModbusLink_outstanding(link) > peak) peak = XVfdModbusLink_outstanding(link);
            }
            tlog("实测: 2s 内双站状态更新 %d 次（单站最小间隔 %llu/%llu ms）"
                 " 链路在途峰值=%d（表深 %d）",
                 g_d[0].statusCount + g_d[1].statusCount - c0,
                 (unsigned long long)g_statusGapMs[0], (unsigned long long)g_statusGapMs[1],
                 peak, XVFD_MODBUS_LINK_PENDING_CAP);
        }

        /* ---------- ③ 双站调速互不串扰 ---------- */
        g_case = "setFreqBoth";
        g_d[0].doneCount = 0;
        g_d[1].doneCount = 0;
        CHECK(XVfdDevice_setFrequency(v1, 25.0), "dev1 发起 25.0Hz（先读 P0.10 再写 1000H）");
        CHECK(XVfdDevice_setFrequency(v2, 10.0), "dev2 发起 10.0Hz");
        WAIT_FN(g_d[0].doneCount > 0 && g_d[1].doneCount > 0, 8000);
        CHECK(g_d[0].lastOk && g_d[0].lastOp == (int)XVfdOperation_SetFrequency, "dev1 设定完成");
        CHECK(g_d[1].lastOk && g_d[1].lastOp == (int)XVfdOperation_SetFrequency, "dev2 设定完成");
        WAIT_FN(XVfdDevice_status_const(v1)->m_outputFreqHz > 24.9 &&
                XVfdDevice_status_const(v1)->m_outputFreqHz < 25.1 &&
                XVfdDevice_status_const(v2)->m_outputFreqHz > 9.9 &&
                XVfdDevice_status_const(v2)->m_outputFreqHz < 10.1, 8000);
        CHECK(XVfdDevice_status_const(v1)->m_outputFreqHz > 24.9 &&
              XVfdDevice_status_const(v1)->m_outputFreqHz < 25.1, "dev1 输出频率回读 25.0Hz");
        CHECK(XVfdDevice_status_const(v2)->m_outputFreqHz > 9.9 &&
              XVfdDevice_status_const(v2)->m_outputFreqHz < 10.1, "dev2 输出频率回读 10.0Hz（隔离）");

        /* ---------- ④ 双站启动隔离 ---------- */
        g_case = "startBoth";
        g_d[0].doneCount = 0;
        g_d[1].doneCount = 0;
        CHECK(XVfdDevice_start(v1, false), "dev1 发起正转");
        CHECK(XVfdDevice_start(v2, true), "dev2 发起反转");
        WAIT_FN(g_d[0].doneCount > 0 && g_d[1].doneCount > 0, 8000);
        CHECK(g_d[0].lastOk, "dev1 启动受理");
        CHECK(g_d[1].lastOk, "dev2 启动受理");
        WAIT_FN(XVfdDevice_status_const(v1)->m_runState == XVfdRunState_RunningForward &&
                XVfdDevice_status_const(v2)->m_runState == XVfdRunState_RunningReverse, 8000);
        CHECK(XVfdDevice_status_const(v1)->m_runState == XVfdRunState_RunningForward, "dev1 正转（3000H=1）");
        CHECK(XVfdDevice_status_const(v2)->m_runState == XVfdRunState_RunningReverse, "dev2 反转（3000H=2，隔离）");

        /* ---------- ⑤ dev1 停机 ---------- */
        g_case = "stop";
        g_d[0].doneCount = 0;
        CHECK(XVfdDevice_stop(v1), "dev1 发起受控停机");
        WAIT_FN(g_d[0].doneCount > 0, 6000);
        CHECK(g_d[0].lastOk, "dev1 停机完成");
        WAIT_FN(XVfdDevice_status_const(v1)->m_runState == XVfdRunState_Stopped, 6000);
        CHECK(XVfdDevice_status_const(v1)->m_runState == XVfdRunState_Stopped, "dev1 回到 Stopped");
        CHECK(XVfdDevice_status_const(v2)->m_runState == XVfdRunState_RunningReverse,
              "dev2 仍反转（停机不越站）");

        /* ---------- ⑥ 故障注入 u1 ---------- */
        g_case = "fault";
        {
            char path[256];
            snprintf(path, sizeof(path), "%s/fault_u1", M512_BASE);
            writeFile(path, "3");   /* 8000H=3 减速过电流 */
        }
        WAIT_FN(XVfdDevice_status_const(v1)->m_faultCode == 3, 10000);
        CHECK(XVfdDevice_status_const(v1)->m_faulted, "dev1 故障置位");
        CHECK(XVfdDevice_status_const(v1)->m_faultCode == 3, "dev1 故障码 = 3");
        {
            const char* name = XVfdM512_faultName(3);
            const char* text = XVfdM512_faultText(3);
            CHECK(name && strcmp(name, "E.oCdF") == 0, "故障名映射 E.oCdF");
            CHECK(text && strcmp(text, "减速过电流") == 0, "故障说明映射");
        }
        CHECK(!XVfdDevice_status_const(v2)->m_faulted, "dev2 无故障（隔离）");

        /* ---------- ⑦ dev1 故障复位 ---------- */
        g_case = "resetFault";
        g_d[0].doneCount = 0;
        CHECK(XVfdDevice_resetFault(v1), "dev1 发起故障复位（2000H=7）");
        WAIT_FN(g_d[0].doneCount > 0, 6000);
        CHECK(g_d[0].lastOk, "dev1 复位完成");
        WAIT_FN(XVfdDevice_status_const(v1)->m_faultCode == 0, 8000);
        CHECK(XVfdDevice_status_const(v1)->m_faultCode == 0, "dev1 故障清除");

        /* ---------- ⑧ dev2 参数读 P0.10 ---------- */
        g_case = "readParam";
        g_d[1].paramReadCount = 0;
        CHECK(XVfdDevice_readParam(v2, 3), "dev2 发起参数读 P0.10（id=3）");
        WAIT_FN(g_d[1].paramReadCount > 0, 6000);
        CHECK(g_d[1].paramReadOk && g_d[1].paramReadId == 3, "dev2 参数读完成");
        CHECK(g_d[1].paramReadRaw == 5000, "P0.10 回读 raw=5000（50.00Hz）");

        /* ---------- ⑨ dev2 参数写 P8.00=300 并回读 ---------- */
        g_case = "writeParam";
        g_d[1].paramWriteCount = 0;
        CHECK(XVfdDevice_writeParam(v2, 11, 300), "dev2 发起参数写 P8.00=300（3.00Hz）");
        WAIT_FN(g_d[1].paramWriteCount > 0, 6000);
        CHECK(g_d[1].paramWriteOk && g_d[1].paramWriteId == 11, "dev2 参数写完成");
        g_d[1].paramReadCount = 0;
        CHECK(XVfdDevice_readParam(v2, 11), "dev2 回读 P8.00");
        WAIT_FN(g_d[1].paramReadCount > 0, 6000);
        CHECK(g_d[1].paramReadOk && g_d[1].paramReadRaw == 300, "P8.00 回读 = 300（写入一致）");

        /* ---------- ⑩a 任意优先级：父类队列有序插入语义 ---------- */
        g_case = "priorityAny";
        {
            /* 直接对父类队列验证：任意整数优先级，值越大越先出，同级 FIFO。
             * （经 XModbusClient_serialQueuePush/Pop 公共 API，与传输无关） */
            /* 用独立 client 实例测队列语义（不碰在用的主站）；
             * 以 serverAddress 作顺序标记，request/reply 传 NULL（安全）。 */
            XModbusClient* qc = XModbusClient_create();
            int order[4];
            int i2;
            CHECK(qc != NULL, "队列语义测试用 client 创建");
            if (qc) {
                XModbusClient_serialQueuePush(qc, NULL, NULL, 11, 5);
                XModbusClient_serialQueuePush(qc, NULL, NULL, 22, 100);
                XModbusClient_serialQueuePush(qc, NULL, NULL, 33, 5);
                XModbusClient_serialQueuePush(qc, NULL, NULL, 44, 50);
                CHECK(XModbusClient_serialQueueCount(qc) == 4, "队列入队 4 条");
                for (i2 = 0; i2 < 4; ++i2) {
                    int addr = 0;
                    if (!XModbusClient_serialQueuePop(qc, NULL, NULL, &addr)) break;
                    order[i2] = addr;
                }
                CHECK(order[0] == 22 && order[1] == 44 && order[2] == 11 && order[3] == 33,
                      "出队顺序 = 优先级降序(100,50,5,5)，同级 FIFO");
                XClassDelete((XClass*)qc);
            }
        }

        /* ---------- ⑩ 跨设备插队：动作(High) 优先于轮询(Normal) ---------- */
        g_case = "priority";
        {
            /* 站 2 持续轮询制造排队；此时向站 1 下发动作（High）：
             * 断言站 1 的动作在站 2 的下一条轮询之前被从站收到
             * （模拟器按到达顺序记录请求日志，动作行应先于其后轮询行）。 */
            uint64_t t0 = XDateTime_currentMSecsSinceEpoch();
            int linesBefore = 0;
            int idxAction = -1;
            int idxPollAfter = -1;
            int i;
            FILE* of;
            char path[256];
            char line[64];
            /* 先数一遍动作前的请求行数 */
            snprintf(path, sizeof(path), "%s/req_order.log", M512_BASE);
            of = fopen(path, "r");
            if (of) {
                while (fgets(line, sizeof(line), of)) ++linesBefore;
                fclose(of);
            }
            while (XDateTime_currentMSecsSinceEpoch() - t0 < 1200) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
            }
            g_d[0].doneCount = 0;
            CHECK(XVfdDevice_start(v1, false), "dev1 发起动作（High 优先）");
            WAIT_FN(g_d[0].doneCount > 0, 6000);
            CHECK(g_d[0].lastOk, "dev1 动作完成（High 插队未被轮询拖死）");
            /* 断言：动作帧(u1 fc6) 出现在其后第一条站 2 轮询帧(u2 fc3) 之前 */
            of = fopen(path, "r");
            if (of) {
                i = 0;
                while (fgets(line, sizeof(line), of)) {
                    if (i >= linesBefore) {
                        if (idxAction < 0 && strstr(line, "u1 fc6")) idxAction = i - linesBefore;
                        else if (idxAction >= 0 && idxPollAfter < 0 && strstr(line, "u2 fc3"))
                            idxPollAfter = i - linesBefore;
                    }
                    ++i;
                }
                fclose(of);
            }
            CHECK(idxAction >= 0, "从站收到站 1 的动作帧（u1 fc6）");
            CHECK(idxAction >= 0 && (idxPollAfter < 0 || idxAction < idxPollAfter),
                  "动作帧(High) 排在站 2 轮询帧(Normal) 之前——优先级插队生效");
        }

        /* ---------- ⑩ 从站静默：离线退避 + 隔离 + 恢复 ---------- */
        g_case = "slaveLost";
        {
            char path[256];
            int status1Before;
            snprintf(path, sizeof(path), "%s/mute", M512_BASE);
            writeFile(path, "2");   /* 静默站 2 */
            status1Before = g_d[0].statusCount;
            WAIT_FN(g_lostUnit == 2, 20000);
            CHECK(g_lostUnit == 2, "链路 slaveLost(2)（连续超时退避）");
            CHECK(g_d[0].statusCount > status1Before, "站 1 轮询不受牵连（离线隔离）");
            {
                int status2Stall = g_d[1].statusCount;
                uint64_t snap = XDateTime_currentMSecsSinceEpoch() + 1500;
                while (XDateTime_currentMSecsSinceEpoch() < snap) {
                    XCoreApplication_processEvents(XEventLoop_AllEvents);
                }
                CHECK(g_d[1].statusCount == status2Stall, "站 2 轮询静默暂停（退避跳拍）");
            }
            /* 解除静默 → 退避到期探活 → slaveAlive(2) + 轮询恢复 */
            unlink(path);
            g_aliveUnit = -1;
            WAIT_FN(g_aliveUnit == 2, 15000);
            CHECK(g_aliveUnit == 2, "链路 slaveAlive(2)（探活恢复）");
            WAIT_FN(XVfdDevice_status_const(v2)->m_outputFreqHz > 9.9 &&
                    XVfdDevice_status_const(v2)->m_outputFreqHz < 10.1, 8000);
            CHECK(XVfdDevice_status_const(v2)->m_outputFreqHz > 9.9 &&
                  XVfdDevice_status_const(v2)->m_outputFreqHz < 10.1, "站 2 轮询恢复");
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
