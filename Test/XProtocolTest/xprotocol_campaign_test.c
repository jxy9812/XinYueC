/**
 * @file       xprotocol_campaign_test.c
 * @brief      战役 1028 XProtocol 端到端联调测试矩阵（S1..S13）。
 * @details    拓扑：Python 多模式虚拟从站（mbtest_campaign_slave.py，先于本
 *             程序由 mbtest_campaign_run.sh 启动）提供 3 条 pty 虚拟 485
 *             总线（bus1/bus2/bus3，各 unit1..8）+ 1 个 Modbus/TCP 监听。
 *             pty 端路径读 $BASE/pty{1,2,3}_path；控制接口为 $BASE 下文件
 *             触摸式开关（busN_mute_uK / busN_badcrc_uK / busN_swalloww_uK /
 *             bus2_closem）；S5/S6 的时序断言以从站帧日志
 *             （frame_bus1.log）字节偏移切片为准（跨进程不比对时钟）。
 *             场景相位（每场景独立相位，失败打印场景名+期望/实际，
 *             进程退出码=失败总数）：
 *               S1 广播回合后续泵：广播写→同 client 普通读照常应答；
 *               S2 重试耗尽续泵+停顿有界：死从站读耗尽重试 error 终结后，
 *                  同 client 活从站下一请求 3s 宽松时限内应答；
 *               S3 CRC 损坏：坏帧→reply 恰好一次以 error 终结、后续轮询恢复；
 *               S4 发送失败不遗弃：A 在途断链（提交后关 pty master→reply 以
 *                  error 终结）+ B 死链路写（关后提交→write 失败路径收口），
 *                  随后切活链路验证单飞恢复；
 *               S5 共口死从站退避：unitLost(5)→静默期 unit6 轮询继续且
 *                  帧日志无 unit5 请求帧→探活 unitAlive(5)；
 *               S6 写优先级：轮询读压队后提交高优写→帧日志断言写帧插队；
 *               S7 writeStalled 信号：从站吞写→attempts 达阈值恰发一次
 *                  stalled（重发继续），解吞后落定；
 *               S8 XVfd 生命周期：detach 后旧应答不回调（计数断言）+
 *                  在途时销毁设备/链路不崩溃（ASan 泄漏随进程退出核查）；
 *               S9 串口双开守卫：同实例二次 open 返回 false（OpenError、
 *                  不发 errorOccurred）；
 *               S10 微浸泡：TCP 连接抖动 + RTU CRC 注入循环 50 轮零崩溃。
 *               S11 未连接同步终态收口：读/写/广播写同步 done(false) 且不占
 *                  在途登记（广播写断链不假报 ok，回归检查次序前移）；
 *               S12 Hub 断链重连：在途终结不滞留 inflight（断开→重连后
 *                  pollOk 恢复增长，守卫单飞闸不被滞留在途挡死）；
 *               S13 设备层断链续拍：断链 PollDue 不卡死（单飞不被同步收口
 *                  复置忙）→静默退避→重连轮询解析恢复。
 *             全部用例走统一事件循环（单线程，与库同款）；断言失败累计。
 *
 *             ⚠ 信号槽约定（约束文档）：槽第一参语义由连接 API 决定——
 *             本程序一律 connect_2（第一参=真 sender）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XDateTime.h"
#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XModbusTcpClient.h"
#include "XModbusRtuSerialClient.h"
#include "XModbusDevice.h"
#include "XModbusReply.h"
#include "XSerialPort.h"
#include "XIODevice.h"
#include "XVfdModbusLink.h"
#include "XVfdModbusBase_Protected.h"
#include "XVariant.h"

/* ==================== 基础设施 ==================== */

static int g_fail = 0;
static const char* g_case = "";
static int g_sceneFailBase = 0;
static uint64_t g_bootMs = 0;
static char g_base[256] = "/tmp/mbsoak/campaign1028";   /* 战役产物根目录 */
static char g_pty1[256];                                /* bus1 从端路径 */
static char g_pty2[256];                                /* bus2 从端路径 */
static char g_pty3[256];                                /* bus3 从端路径 */
static int g_tcpPort = 15431;                           /* 战役固定端口段 */

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", msg); fflush(stdout); } \
    else { printf("  [FAIL] %s (场景=%s@%d)\n", msg, g_case, __LINE__); fflush(stdout); ++g_fail; } \
} while (0)

#define CHECK_EQ(actual, want, msg) do { \
    long long va_ = (long long)(actual), vw_ = (long long)(want); \
    if (va_ == vw_) { printf("  [OK] %s\n", msg); fflush(stdout); } \
    else { printf("  [FAIL] %s 期望=%lld 实际=%lld (场景=%s@%d)\n", \
                  msg, vw_, va_, g_case, __LINE__); fflush(stdout); ++g_fail; } \
} while (0)

static void tlog(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("[%8.3fs] ", (double)(XDateTime_currentMSecsSinceEpoch() - g_bootMs) / 1000.0);
    vprintf(fmt, ap); printf("\n"); fflush(stdout);
}

typedef bool (*Pred)(void* ctx);

/** @brief 事件循环内等待谓词成立（超时返回假；2ms 节拍自旋）。
 *  @note 不用 WaitForMoreEvents：无事件时无限阻塞会饿死定时器拍（实测）。 */
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

/** @brief 泵事件循环固定时长（场景内定时窗用）。 */
static void pumpForMs(uint64_t ms)
{
    const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + ms;
    while (XDateTime_currentMSecsSinceEpoch() < deadline)
        XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/** @brief 场景相位起止（失败计数按场景切账打印）。 */
static void sceneBegin(const char* name, const char* title)
{
    g_case = name; g_sceneFailBase = g_fail;
    printf("== %s %s ==\n", name, title); fflush(stdout);
}
static void sceneEnd(void)
{
    printf("== %s 完成: 失败 %d 项 ==\n", g_case, g_fail - g_sceneFailBase);
    fflush(stdout);
}

/* ==================== 控制文件 / 帧日志工具 ==================== */

static void ctrlSet(const char* name)
{
    char path[512]; snprintf(path, sizeof(path), "%s/%s", g_base, name);
    FILE* f = fopen(path, "w");
    if (f) { fputs("1", f); fclose(f); }
}
static void ctrlClear(const char* name)
{
    char path[512]; snprintf(path, sizeof(path), "%s/%s", g_base, name);
    remove(path);
}

/** @brief 从站帧日志路径（bus=1..3）。 */
static const char* frameLogPath(int bus)
{
    static char path[512];
    snprintf(path, sizeof(path), "%s/frame_bus%d.log", g_base, bus);
    return path;
}

/** @brief 取文件字节数（读不到返回 -1）。 */
static long fileSizeOf(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    const long n = ftell(f);
    fclose(f);
    return n;
}

/** @brief 泵事件循环等待文件字节数超过 from（超时返回当前字节数）。
 *  @note 跨进程从站逐帧落盘，以「文件增长」为窗界事件，消除固定窗观测抖动。 */
static long waitFileGrow(const char* path, long from, int timeoutMs)
{
    const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)timeoutMs;
    for (;;) {
        const long n = fileSizeOf(path);
        if (n > from) return n;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        const uint64_t spin = XDateTime_currentMSecsSinceEpoch() + 2;
        while (XDateTime_currentMSecsSinceEpoch() < spin) { /* 2ms 节拍自旋 */ }
        if (XDateTime_currentMSecsSinceEpoch() >= deadline) return n;
    }
}

/** @brief 统计帧日志 [from,to) 字节切片中含 needle 的行数（S5 断言用）。 */
static int sliceCountNeedle(const char* path, long from, long to, const char* needle)
{
    static char buf[65536];
    if (to <= from) return 0;
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, from, SEEK_SET) != 0) { fclose(f); return -1; }
    const size_t want = (size_t)(to - from);
    const size_t got = fread(buf, 1, want < sizeof(buf) - 1 ? want : sizeof(buf) - 1, f);
    fclose(f);
    buf[got] = 0;
    int count = 0;
    char* save = NULL;
    for (char* line = strtok_r(buf, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        if (strstr(line, needle)) ++count;
    }
    return count;
}

typedef struct { int addr; int fc; } LogRx;

/** @brief 提取帧日志 [from,to) 切片内全部 RX 行的 (addr,fc) 序列（S6 断言用）。
 *  @return RX 行数（超 cap 截断）；读不到文件返回 -1。 */
static int sliceRxList(const char* path, long from, long to, LogRx* out, int cap)
{
    static char buf[65536];
    if (to <= from) return 0;
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, from, SEEK_SET) != 0) { fclose(f); return -1; }
    const size_t want = (size_t)(to - from);
    const size_t got = fread(buf, 1, want < sizeof(buf) - 1 ? want : sizeof(buf) - 1, f);
    fclose(f);
    buf[got] = 0;
    int n = 0;
    char* save = NULL;
    for (char* line = strtok_r(buf, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        const char* rx = strstr(line, " RX addr=");
        if (!rx) continue;
        int addr = -1, fc = -1;
        if (sscanf(rx, " RX addr=%d fc=%d", &addr, &fc) == 2 && n < cap) {
            out[n].addr = addr; out[n].fc = fc; ++n;
        }
    }
    return n;
}

/* ==================== 回复观察（finished 计数 + 错误/数据捕获） ==================== */

typedef struct {
    XModbusReply* reply;
    int finished;       /* finished 信号次数（恰好一次断言） */
    int lastError;      /* 终结错误码（XModbusDevice_Error） */
    int nInter;         /* 中间错误个数（-1=未取） */
    uint16_t val0;      /* 结果第 0 值（有结果时） */
    bool hasVal;
} ReplyWatch;

static ReplyWatch g_watches[8];

static void onWatchFinished(XObject* sender, XVarList* args)
{
    (void)args;
    for (int i = 0; i < 8; ++i) {
        if (g_watches[i].reply != (XModbusReply*)sender) continue;
        XModbusReply* r = (XModbusReply*)sender;
        ++g_watches[i].finished;
        g_watches[i].lastError = (int)XModbusReply_error(r);
        g_watches[i].nInter = 0;
        XVector* inter = XModbusReply_intermediateErrors(r);
        if (inter) {
            g_watches[i].nInter = (int)XVector_size_base((const XContainer*)inter);
            XClassDelete((XClass*)inter);
        }
        const XModbusDataUnit* res = XModbusReply_result_const(r);
        if (res) {
            g_watches[i].val0 = XModbusDataUnit_value(res, 0);
            g_watches[i].hasVal = true;
        }
        /* 持有方按框架约定 deleteLater 延迟释放。 */
        XObject_deleteLater(sender);
        /* 立即遗忘指针：延迟释放后地址会被分配器复用，残留指针会让新 reply
         * 的 finished 误命中旧槽（按下标先后抢占），新槽饿死。 */
        g_watches[i].reply = NULL;
        return;
    }
}

static void watch(int slot, XModbusReply* r)
{
    memset(&g_watches[slot], 0, sizeof(ReplyWatch));
    g_watches[slot].reply = r;
    g_watches[slot].lastError = -1;
    g_watches[slot].nInter = -1;
    if (r) XObject_connect_2((XObject*)r, XSignal(XModbusReply_finished_signal),
                             onWatchFinished);
}

/** @brief 带同步终态补记的挂观察。
 *  @note finished 是同步直达持有方的（见 XModbusRtuSerialClient.c 收口注释）：
 *        在 submit 内部即终结的 reply（如发送失败/未连接收口）其 finished
 *        先于本观察连接到达，信号计数会漏记。此处按「恰好一次终态」契约
 *        对已终态 reply 补记捕获（错误码/中间错误/数据）。 */
static void watchLate(int slot, XModbusReply* r)
{
    watch(slot, r);
    if (r && XModbusReply_isFinished(r)) {
        g_watches[slot].finished = 1;
        g_watches[slot].lastError = (int)XModbusReply_error(r);
        g_watches[slot].nInter = 0;
        XVector* inter = XModbusReply_intermediateErrors(r);
        if (inter) {
            g_watches[slot].nInter = (int)XVector_size_base((const XContainer*)inter);
            XClassDelete((XClass*)inter);
        }
        const XModbusDataUnit* res = XModbusReply_result_const(r);
        if (res) {
            g_watches[slot].val0 = XModbusDataUnit_value(res, 0);
            g_watches[slot].hasVal = true;
        }
    }
}

static bool predWatchDone(void* ctx)
{
    ReplyWatch* w = (ReplyWatch*)ctx;
    return w->finished > 0;
}
static bool predWatchDoneOk(void* ctx)
{
    ReplyWatch* w = (ReplyWatch*)ctx;
    return w->finished > 0 && w->lastError == (int)XModbusDevice_NoError;
}

/** @brief 挂死诊断：打印设备状态/reply 终态/客户端在途指针（排查用）。 */
static void diagClient(const char* tag, XModbusRtuSerialClient* c, XModbusReply* r)
{
    tlog("%s diag: state=%d replyFinished=%d clientCurrentReply=%p", tag,
         c ? (int)XModbusDevice_state((XModbusDevice*)c) : -1,
         r ? (int)XModbusReply_isFinished(r) : -1,
         (void*)(c ? c->m_currentReply : NULL));
}

/* ==================== RTU 客户端 / 请求工具 ==================== */

/** @brief 造一个 RTU 串口客户端（未连接；串口参数指向 ptyPath）。 */
static XModbusRtuSerialClient* rtuMake(const char* ptyPath, int timeoutMs,
                                       int retries, bool autoReconnect)
{
    XModbusRtuSerialClient* c = XModbusRtuSerialClient_create();
    if (!c) return NULL;
    XModbusClient_setTimeout((XModbusClient*)c, (size_t)timeoutMs);
    XModbusClient_setNumberOfRetries((XModbusClient*)c, (uint8_t)retries);
    XModbusClient_setAutoReconnect((XModbusClient*)c, autoReconnect);
    XModbusRtuSerialClient_setTurnaroundDelay(c, 60);
    XModbusDevice_setConnectionParameter_ref((XModbusDevice*)c,
        XModbusDevice_SerialPortNameParameter, XVariant_create_utf8_str(ptyPath));
    XModbusDevice_setConnectionParameter_ref((XModbusDevice*)c,
        XModbusDevice_SerialBaudRateParameter, XVariant_create_int(9600));
    return c;
}

static bool predConnected(void* ctx)
{
    return XModbusDevice_state((XModbusDevice*)ctx) == XModbusDevice_ConnectedState;
}
static bool predUnconnected(void* ctx)
{
    return XModbusDevice_state((XModbusDevice*)ctx) == XModbusDevice_UnconnectedState;
}

/** @brief 连接 RTU 客户端并确认 Connected（同步 open，事件泵至多等 2s）。 */
static bool rtuOpen(XModbusRtuSerialClient* c)
{
    if (!c || !XModbusDevice_connectDevice((XModbusDevice*)c)) return false;
    return waitUntil(predConnected, c, 2000);
}

/** @brief 断开并延迟释放客户端（端口释放，供下一场景复用同 pty）。 */
static void rtuClose(XModbusRtuSerialClient* c)
{
    if (!c) return;
    XModbusDevice_disconnectDevice((XModbusDevice*)c);
    XObject_deleteLater((XObject*)c);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/** @brief 提交 FC03 读保持寄存器（栈上临时单元，createReply 深拷贝收编）。 */
static XModbusReply* sendReadHr(XModbusClient* c, int unit, uint16_t addr,
                                uint16_t count, int priority)
{
    XModbusDataUnit u;
    XModbusDataUnit_init_ex(&u, XModbusHoldingRegisters, addr, count);
    XModbusReply* r = XModbusClient_sendReadRequest(c, &u, unit,
                                                    (XModbusRequestPriority)priority);
    XClassDeinit(&u);
    return r;
}

/** @brief 提交 FC06 写单寄存器（保持寄存器）。 */
static XModbusReply* sendWriteHr(XModbusClient* c, int unit, uint16_t addr,
                                 uint16_t value, int priority)
{
    XModbusDataUnit u;
    XModbusDataUnit_init_ex(&u, XModbusHoldingRegisters, addr, 1);
    XModbusDataUnit_setValue(&u, 0, value);
    XModbusReply* r = XModbusClient_sendWriteRequest(c, &u, unit,
                                                     (XModbusRequestPriority)priority);
    XClassDeinit(&u);
    return r;
}

/* ==================== Hub 信号计数 ==================== */

static int g_lostCnt, g_aliveCnt, g_stalledCnt, g_hubWriteOk;
static int g_lostDev, g_lostUnit, g_aliveDev, g_aliveUnit, g_stalledAttempts;

static void onUnitLost(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_args_2(args, int, devId, int, unitId);
    ++g_lostCnt; g_lostDev = devId; g_lostUnit = unitId;
    printf("  [SIG] unitLost dev=%d unit=%d\n", devId, unitId); fflush(stdout);
}
static void onUnitAlive(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_args_2(args, int, devId, int, unitId);
    ++g_aliveCnt; g_aliveDev = devId; g_aliveUnit = unitId;
    printf("  [SIG] unitAlive dev=%d unit=%d\n", devId, unitId); fflush(stdout);
}
static void onWriteStalled(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_args_4(args, int, type, int, vStart, int, count, int, attempts);
    (void)type; (void)vStart; (void)count;
    ++g_stalledCnt; g_stalledAttempts = attempts;
    printf("  [SIG] writeStalled attempts=%d\n", attempts); fflush(stdout);
}
static void onHubWriteCompleted(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_args_4(args, int, type, int, vStart, int, count, bool, ok);
    (void)type; (void)vStart; (void)count;
    if (ok) ++g_hubWriteOk;
}

static bool predLost(void* ctx) { (void)ctx; return g_lostCnt > 0; }
static bool predAlive(void* ctx) { (void)ctx; return g_aliveCnt > 0; }
static bool predStalled(void* ctx) { (void)ctx; return g_stalledCnt > 0; }

typedef struct { XModbusHub* hub; int devId; uint32_t base; } PredOkGrow;
static bool predPollOkGrew(void* ctx)
{
    PredOkGrow* p = (PredOkGrow*)ctx;
    return XModbusHub_pollOkCount(p->hub, p->devId) > p->base;
}
static bool predPendingZero(void* ctx)
{
    return XModbusHub_pendingWriteCount((XModbusHub*)ctx) == 0;
}

/** @brief Hub + bus1 RTU 客户端一键搭台（客户端所有权转移 Hub）。
 *  @return Hub（*outDevId=设备句柄）；失败返回 NULL。 */
static XModbusHub* hubMake(const char* ptyPath, int timeoutMs, int* outDevId)
{
    XModbusRtuSerialClient* c = rtuMake(ptyPath, timeoutMs, 0, true);
    if (!c || !rtuOpen(c)) { rtuClose(c); return NULL; }
    XModbusHub* hub = XModbusHub_create();
    if (!hub) { rtuClose(c); return NULL; }
    const int id = XModbusHub_addDevice(hub, (XModbusClient*)c, "rtu");
    if (id <= 0) { XModbusHub_deleteLater((XObject*)hub); rtuClose(c); return NULL; }
    if (outDevId) *outDevId = id;
    return hub;
}

/** @brief Hub 收摊（停轮询→断链→延迟释放→事件泵两轮清 deleteLater 链）。 */
static void hubShutdown(XModbusHub* hub)
{
    if (!hub) return;
    XModbusHub_stopPolling(hub);
    XModbusHub_disconnectAll(hub);
    XModbusHub_deleteLater((XObject*)hub);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/* ==================== S8 链路完成回调计数 ==================== */

typedef struct { int n; int lastOk; } DoneCtx;
static void onLinkDone(XObject* owner, void* userData, bool ok,
                       const XModbusDataUnit* result)
{
    (void)owner; (void)result;
    DoneCtx* ctx = (DoneCtx*)userData;
    if (!ctx) return;
    ++ctx->n; ctx->lastOk = ok ? 1 : 0;
}
static bool predDoneN(void* ctx) { return ((DoneCtx*)ctx)->n >= 1; }

/* ==================== S9 串口错误信号计数 ==================== */

static int g_serialErrCnt;
static void onSerialError(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    ++g_serialErrCnt;
}

/* ==================== S1 广播回合后续泵 ==================== */

static void testS1(void)
{
    sceneBegin("S1", "广播回合后续泵（广播写→同 client 普通读照常应答）");
    XModbusRtuSerialClient* c = rtuMake(g_pty1, 600, 1, false);
    CHECK(c != NULL, "rtu client create");
    if (!c) { sceneEnd(); return; }
    CHECK(rtuOpen(c), "bus1 连接");

    /* 广播写（unit=0）：无应答，turnaround 后收口。 */
    XModbusReply* w = sendWriteHr((XModbusClient*)c, 0, 0, 0xBEEF,
                                  XModbusRequestPriority_Normal);
    CHECK(w != NULL, "广播写受理(unit=0)");
    watchLate(0, w);
    /* 同一拍内提交普通读：此刻广播回合仍占单飞 → 读必须排队，广播回合
     * 收口后续泵送上总线（回归队列停摆根修）。 */
    XModbusReply* r = sendReadHr((XModbusClient*)c, 1, 0, 1,
                                 XModbusRequestPriority_Normal);
    CHECK(r != NULL, "广播后普通读受理(unit=1)");
    watchLate(1, r);

    CHECK(waitUntil(predWatchDone, &g_watches[0], 2000),
          "广播回合收口（turnaround 到期）");
    CHECK_EQ(g_watches[0].finished, 1, "广播 reply 恰好一次 finished");
    const bool readOk = waitUntil(predWatchDoneOk, &g_watches[1], 3000);
    CHECK(readOk, "广播后普通读 3s 内照常应答（续泵）");
    CHECK_EQ(g_watches[1].finished, 1, "读 reply 恰好一次 finished");
    CHECK_EQ(g_watches[1].lastError, XModbusDevice_NoError, "读无错误");
    CHECK(g_watches[1].hasVal && g_watches[1].val0 == 0xBEEF,
          "广播写已生效（unit1 HR0=0xBEEF）");

    rtuClose(c);
    sceneEnd();
}

/* ==================== S2 重试耗尽续泵 + 停顿有界 ==================== */

static void testS2(void)
{
    sceneBegin("S2", "重试耗尽续泵+停顿有界（死从站读 error 终结→活从站 3s 内应答）");
    XModbusRtuSerialClient* c = rtuMake(g_pty1, 300, 0, false);
    CHECK(c != NULL && rtuOpen(c), "bus1 连接（timeout=300 retries=0）");
    if (!c) { sceneEnd(); return; }

    ctrlSet("bus1_mute_u2");
    XModbusReply* dead = sendReadHr((XModbusClient*)c, 2, 0, 2,
                                    XModbusRequestPriority_Normal);
    CHECK(dead != NULL, "死从站(unit2)读受理");
    watchLate(0, dead);
    CHECK(waitUntil(predWatchDone, &g_watches[0], 2000),
          "死从站读在重试耗尽后终结（≤2s）");
    CHECK_EQ(g_watches[0].finished, 1, "死读 reply 恰好一次 finished（不悬挂）");
    CHECK(g_watches[0].lastError != (int)XModbusDevice_NoError,
          "死读以 error 终结");

    /* 同 client 立即追活从站读：续泵保证下一请求 3s 宽松时限内应答。 */
    XModbusReply* live = sendReadHr((XModbusClient*)c, 1, 0, 2,
                                    XModbusRequestPriority_Normal);
    CHECK(live != NULL, "活从站(unit1)读受理");
    watchLate(1, live);
    CHECK(waitUntil(predWatchDoneOk, &g_watches[1], 3000),
          "活从站读 3s 宽松时限内应答（续泵+停顿有界）");
    CHECK_EQ(g_watches[1].lastError, XModbusDevice_NoError, "活读无错误");

    ctrlClear("bus1_mute_u2");
    rtuClose(c);
    sceneEnd();
}

/* ==================== S3 CRC 损坏 ==================== */

static void testS3(void)
{
    sceneBegin("S3", "CRC 损坏（坏帧→reply 恰好一次 error 终结→后续轮询恢复）");
    XModbusRtuSerialClient* c = rtuMake(g_pty1, 300, 0, false);
    CHECK(c != NULL && rtuOpen(c), "bus1 连接（timeout=300 retries=0）");
    if (!c) { sceneEnd(); return; }

    ctrlSet("bus1_badcrc_u3");
    XModbusReply* r = sendReadHr((XModbusClient*)c, 3, 0, 2,
                                 XModbusRequestPriority_Normal);
    CHECK(r != NULL, "坏 CRC 从站(unit3)读受理");
    watchLate(0, r);
    CHECK(waitUntil(predWatchDone, &g_watches[0], 2000), "坏 CRC 回合终结（≤2s）");
    CHECK_EQ(g_watches[0].finished, 1,
             "reply 恰好一次以 error 终结（无二次 finished/UAF）");
    CHECK(g_watches[0].lastError != (int)XModbusDevice_NoError, "终结错误非 NoError");
    CHECK(g_watches[0].nInter >= 1, "CRC 错误记入中间错误（未提前终结）");

    ctrlClear("bus1_badcrc_u3");
    XModbusReply* r2 = sendReadHr((XModbusClient*)c, 3, 0, 2,
                                  XModbusRequestPriority_Normal);
    watchLate(1, r2);
    CHECK(waitUntil(predWatchDoneOk, &g_watches[1], 3000),
          "后续轮询恢复（好帧应答）");

    rtuClose(c);
    sceneEnd();
}

/* ==================== S4 发送失败不遗弃 ==================== */

static void testS4(void)
{
    sceneBegin("S4", "发送失败不遗弃（A 在途断链收口 + B 死链路写收口 → 单飞恢复）");
    XModbusRtuSerialClient* c = rtuMake(g_pty2, 400, 0, false);
    CHECK(c != NULL && rtuOpen(c), "bus2 连接");
    if (!c) { sceneEnd(); return; }

    /* 基线读：链路健康。 */
    XModbusReply* base = sendReadHr((XModbusClient*)c, 1, 0, 2,
                                    XModbusRequestPriority_Normal);
    watchLate(0, base);
    CHECK(waitUntil(predWatchDoneOk, &g_watches[0], 2000), "基线读应答");

    /* —— A：提交瞬间（在途）关 pty master —— 静默位保证请求在途不被应答，
     * 随后传输死亡：reply 必须以 error 终结（超时收口），不得永久悬挂。 */
    ctrlSet("bus2_mute_u1");
    XModbusReply* r = sendReadHr((XModbusClient*)c, 1, 0, 2,
                                 XModbusRequestPriority_Normal);
    watchLate(1, r);
    pumpForMs(30);                       /* 帧已上线（write 已成功） */
    ctrlSet("bus2_closem");
    CHECK(waitUntil(predWatchDone, &g_watches[1], 3000),
          "A 在途断链 reply 以 error 终结（≤3s，不得永久无 finished）");
    if (!g_watches[1].finished) diagClient("S4-A 挂死", c, r);
    CHECK_EQ(g_watches[1].finished, 1, "A 失败 reply 恰好一次 finished");
    CHECK(g_watches[1].lastError != (int)XModbusDevice_NoError,
          "A 失败 reply 错误非 NoError");
    ctrlClear("bus2_mute_u1");

    /* —— B：死链路写 —— 关 master 后再提交：write 必须失败且该 reply 以
     * error 终态收口（[A3] 发送失败不遗弃），不得遗弃在途。 */
    ctrlClear("bus2_closem");
    pumpForMs(100);
    XModbusDevice_disconnectDevice((XModbusDevice*)c);
    XModbusDevice_setConnectionParameter_ref((XModbusDevice*)c,
        XModbusDevice_SerialPortNameParameter, XVariant_create_utf8_str(g_pty3));
    CHECK(XModbusDevice_connectDevice((XModbusDevice*)c) &&
          waitUntil(predConnected, c, 3000), "B 切 bus3 重连");
    {
        XModbusReply* base3 = sendReadHr((XModbusClient*)c, 1, 0, 2,
                                         XModbusRequestPriority_Normal);
        watchLate(2, base3);
        CHECK(waitUntil(predWatchDoneOk, &g_watches[2], 2000), "B bus3 基线读应答");
    }
    ctrlSet("bus3_closem");
    pumpForMs(250);                      /* 从站侧 20ms 轮询关 fd 生效 */
    XModbusReply* w = sendReadHr((XModbusClient*)c, 1, 0, 2,
                                 XModbusRequestPriority_Normal);
    CHECK(w != NULL, "B 死链路读提交受理");
    watchLate(3, w);
    CHECK(waitUntil(predWatchDone, &g_watches[3], 3000),
          "B 死链路写 reply 以 error 终结（≤3s，不得永久无 finished）");
    if (!g_watches[3].finished) diagClient("S4-B 挂死", c, w);
    CHECK_EQ(g_watches[3].finished, 1, "B 失败 reply 恰好一次 finished");
    CHECK(g_watches[3].lastError != (int)XModbusDevice_NoError,
          "B 失败 reply 错误非 NoError");
    tlog("S4-B 终结错误码=%d（WriteError=%d ConnectionError=%d TimeoutError=%d）",
         g_watches[3].lastError, (int)XModbusDevice_WriteError,
         (int)XModbusDevice_ConnectionError, (int)XModbusDevice_TimeoutError);

    /* —— 恢复：切回 bus1 活链路 → 普通读照常（设备单飞可恢复）。 —— */
    XModbusDevice_disconnectDevice((XModbusDevice*)c);
    XModbusDevice_setConnectionParameter_ref((XModbusDevice*)c,
        XModbusDevice_SerialPortNameParameter, XVariant_create_utf8_str(g_pty1));
    CHECK(XModbusDevice_connectDevice((XModbusDevice*)c) &&
          waitUntil(predConnected, c, 3000), "切回 bus1 重连");
    XModbusReply* r2 = sendReadHr((XModbusClient*)c, 1, 0, 2,
                                  XModbusRequestPriority_Normal);
    watchLate(4, r2);
    CHECK(waitUntil(predWatchDoneOk, &g_watches[4], 3000),
          "恢复后读应答（设备单飞可恢复）");

    ctrlClear("bus2_closem");
    ctrlClear("bus3_closem");
    rtuClose(c);
    sceneEnd();
}

/* ==================== S5 共口死从站退避 ==================== */

static void testS5(void)
{
    sceneBegin("S5", "共口死从站退避（unitLost(5)→静默期 unit6 有界+无 unit5 帧→unitAlive(5)）");
    g_lostCnt = g_aliveCnt = g_lostDev = g_lostUnit = g_aliveDev = g_aliveUnit = 0;
    int id = 0;
    XModbusHub* hub = hubMake(g_pty1, 250, &id);
    CHECK(hub != NULL, "hub + bus1 RTU 客户端搭建");
    if (!hub) { sceneEnd(); return; }
    XObject_connect_2((XObject*)hub, XSignal(XModbusHub_unitLost_signal), onUnitLost);
    XObject_connect_2((XObject*)hub, XSignal(XModbusHub_unitAlive_signal), onUnitAlive);
    XModbusHub_setUnitBackoff(hub, 1, 1000);   /* 1 次失败即静默，静默 1s（库下限） */
    XModbusHub_setPollIntervalDefault(hub, 150);
    XModbusMapping* map = XModbusMapping_create();
    XModbusMapping_addRange(map, XModbusInputRegisters, 0, 2, id, 5, 0);
    XModbusMapping_addRange(map, XModbusInputRegisters, 10, 2, id, 6, 0);
    XModbusHub_setMapping(hub, map);
    /* 设备已随 hubMake 登记，缺省间隔只影响新设备——对已登记设备显式改拍。 */
    CHECK(XModbusHub_setPollInterval(hub, id, 150), "轮询拍改 150ms（已登记设备即时生效）");
    CHECK_EQ(XModbusHub_cacheCount(hub, XModbusInputRegisters), 12, "IR 缓存跨度=12");

    ctrlSet("bus1_mute_u5");
    XModbusHub_startPolling(hub);
    CHECK(waitUntil(predLost, hub, 3000), "unitLost(5) 到达（1 次失败即静默）");
    CHECK_EQ(g_lostDev, id, "失联设备句柄正确");
    CHECK_EQ(g_lostUnit, 5, "失联从站=unit5");

    /* 静默窗 [O1,O2]：帧日志不得出现 unit5 请求帧；unit6 轮询继续。
     * 窗界不取固定时长（观测抖动来源）：以「帧日志增长」为窗尾——泵事件
     * 循环等待增长（2.5s 截止），增长本身就是被测性质（unit6 轮询继续）
     * 的直接证据；仍不增长则该断言以失败呈现（恰为要抓的停摆）。 */
    const char* flog1 = frameLogPath(1);
    const long o1 = fileSizeOf(flog1);
    CHECK(o1 >= 0, "静默窗前帧日志可读");
    const long o2 = waitFileGrow(flog1, o1, 2500);
    CHECK(o2 > o1, "静默期帧日志增长（unit6 轮询继续）");
    CHECK_EQ(sliceCountNeedle(flog1, o1, o2, "addr=5 fc"), 0,
             "静默期从站帧日志无 unit5 请求帧");
    PredOkGrow grow = { hub, id, XModbusHub_pollOkCount(hub, id) };
    CHECK(waitUntil(predPollOkGrew, &grow, 800),
          "静默期 unit6 轮询 pollOk 增长（时延有界）");

    /* 解除静默（撤文件）：静默到期自动探活 → unitAlive(5)。 */
    ctrlClear("bus1_mute_u5");
    CHECK(waitUntil(predAlive, hub, 4000), "静默到期探活成功 unitAlive(5)");
    CHECK_EQ(g_aliveDev, id, "恢复设备句柄正确");
    CHECK_EQ(g_aliveUnit, 5, "恢复从站=unit5");

    hubShutdown(hub);
    sceneEnd();
}

/* ==================== S6 写优先级 ==================== */

static void testS6(void)
{
    sceneBegin("S6", "写优先级（多笔轮询读压队→高优写插队，帧日志断言上线顺序）");
    XModbusRtuSerialClient* c = rtuMake(g_pty1, 800, 0, false);
    CHECK(c != NULL && rtuOpen(c), "bus1 连接");
    if (!c) { sceneEnd(); return; }
    const char* flog1 = frameLogPath(1);
    const long o1 = fileSizeOf(flog1);

    /* 先塞 3 笔普通轮询读（A 在途、B/C 排队），再提交高优写 W。 */
    XModbusReply* ra = sendReadHr((XModbusClient*)c, 1, 0, 1,
                                  XModbusRequestPriority_Normal);
    XModbusReply* rb = sendReadHr((XModbusClient*)c, 1, 2, 1,
                                  XModbusRequestPriority_Normal);
    XModbusReply* rc = sendReadHr((XModbusClient*)c, 1, 4, 1,
                                  XModbusRequestPriority_Normal);
    XModbusReply* rw = sendWriteHr((XModbusClient*)c, 1, 6, 0x1234,
                                   XModbusRequestPriority_High);
    CHECK(ra && rb && rc && rw, "4 笔请求全部受理（A/B/C 读 + W 高优写）");
    watchLate(0, ra); watchLate(1, rb); watchLate(2, rc); watchLate(3, rw);
    CHECK(waitUntil(predWatchDone, &g_watches[0], 4000) &&
          waitUntil(predWatchDone, &g_watches[1], 4000) &&
          waitUntil(predWatchDone, &g_watches[2], 4000) &&
          waitUntil(predWatchDone, &g_watches[3], 4000), "4 笔全部收口");

    LogRx rx[8];
    const int n = sliceRxList(flog1, o1, fileSizeOf(flog1), rx, 8);
    CHECK_EQ(n, 4, "帧日志 RX 帧数=4（无串扰）");
    if (n == 4) {
        printf("  [dbg] 上线顺序: fc%d,fc%d,fc%d,fc%d\n",
               rx[0].fc, rx[1].fc, rx[2].fc, rx[3].fc); fflush(stdout);
        CHECK_EQ(rx[0].fc, 3, "帧1=A 读(FC03) 先上线（提交时在途）");
        CHECK_EQ(rx[1].fc, 6, "帧2=W 写(FC06) 插队先于后入队读");
        CHECK_EQ(rx[2].fc, 3, "帧3=B 读 在写后上线");
        CHECK_EQ(rx[3].fc, 3, "帧4=C 读 最后上线");
    } else {
        CHECK(false, "RX 帧数非 4，顺序断言跳过（见上方帧数）");
    }

    rtuClose(c);
    sceneEnd();
}

/* ==================== S7 writeStalled 信号 ==================== */

static void testS7(void)
{
    sceneBegin("S7", "writeStalled 信号（吞写→attempts 达阈值恰发一次，重发继续）");
    g_stalledCnt = g_stalledAttempts = 0; g_hubWriteOk = 0;
    int id = 0;
    XModbusHub* hub = hubMake(g_pty1, 250, &id);
    CHECK(hub != NULL, "hub + bus1 RTU 客户端搭建");
    if (!hub) { sceneEnd(); return; }
    XObject_connect_2((XObject*)hub, XSignal(XModbusHub_writeStalled_signal),
                      onWriteStalled);
    XObject_connect_2((XObject*)hub, XSignal(XModbusHub_writeCompleted_signal),
                      onHubWriteCompleted);
    XModbusHub_setWriteRetryInterval(hub, 300);
    XModbusHub_setWriteStallNotice(hub, 2);
    XModbusMapping* map = XModbusMapping_create();
    XModbusMapping_addRange(map, XModbusHoldingRegisters, 0, 4, id, 4, 10);
    XModbusHub_setMapping(hub, map);
    CHECK_EQ(XModbusHub_cacheCount(hub, XModbusHoldingRegisters), 4, "HR 缓存跨度=4");

    ctrlSet("bus1_swalloww_u4");
    XModbusHub_startPolling(hub);  /* 写重发定时器随总闸拉起；无输入区→无轮询帧 */
    CHECK(XModbusHub_writeValue(hub, XModbusHoldingRegisters, 0, 0x5A5A),
          "写意图受理（HR v0=0x5A5A，从站吞写）");
    CHECK(waitUntil(predStalled, hub, 3000), "attempts 达阈值(2) 发出 writeStalled");
    CHECK_EQ(g_stalledCnt, 1, "stalled 信号恰好一次");
    CHECK_EQ(g_stalledAttempts, 2, "stalled 时的 attempts=阈值 2");
    CHECK_EQ(XModbusHub_pendingWriteCount(hub), 1, "意图仍滞留（重发继续）");
    pumpForMs(800);
    CHECK_EQ(g_stalledCnt, 1, "继续滞留期 stalled 不重复发（每意图至多一次）");
    CHECK_EQ(XModbusHub_pendingWriteCount(hub), 1, "滞留意图仍在重发");

    ctrlClear("bus1_swalloww_u4");
    CHECK(waitUntil(predPendingZero, hub, 3000), "解吞后补写落定");
    CHECK(g_hubWriteOk >= 1, "writeCompleted(ok) 到达");
    CHECK_EQ(g_stalledCnt, 1, "落定后 stalled 仍恰好一次");

    hubShutdown(hub);
    sceneEnd();
}

/* ==================== S8 XVfd 生命周期 ==================== */

static void testS8(void)
{
    sceneBegin("S8", "XVfd 生命周期（detach 旧应答不回调 + 在途销毁设备/链路不崩）");
    XModbusRtuSerialClient* c = rtuMake(g_pty1, 400, 0, false);
    CHECK(c != NULL && rtuOpen(c), "bus1 连接");
    if (!c) { sceneEnd(); return; }
    XVfdModbusLink* link = XVfdModbusLink_create();
    CHECK(link != NULL && XVfdModbusLink_setClient(link, (XModbusClient*)c),
          "链路创建并绑定客户端");
    if (!link) { rtuClose(c); sceneEnd(); return; }

    /* —— 基线：owner 提交→回调到达。 —— */
    static DoneCtx ctxA;
    ctxA.n = 0; ctxA.lastOk = -1;
    XObject* ownerA = XObject_create();
    CHECK(ownerA != NULL, "owner 设备对象创建");
    CHECK(XVfdModbusLink_submitRead(link, ownerA, 1, 0, 2, onLinkDone, &ctxA,
                                    XModbusRequestPriority_Normal),
          "基线读提交");
    CHECK(waitUntil(predDoneN, &ctxA, 2000), "基线回调到达");
    CHECK_EQ(ctxA.lastOk, 1, "基线回调 ok");

    /* —— detach：在途（帧已上线未回）时 cancelOwner + 销毁设备 ——
     * 应答随后到达：不得回调已销毁 owner（计数断言），ASan 下无 UAF。 */
    CHECK(XVfdModbusLink_submitRead(link, ownerA, 1, 2, 2, onLinkDone, &ctxA,
                                    XModbusRequestPriority_Normal),
          "在途读提交（unit1 addr=2）");
    CHECK_EQ(XVfdModbusLink_outstanding(link), 1, "在途登记=1");
    CHECK_EQ(XVfdModbusLink_cancelOwner(link, ownerA), 1, "cancelOwner 摘除 1 条");
    CHECK_EQ(XVfdModbusLink_outstanding(link), 0, "摘除后在途=0");
    XObject_deleteLater(ownerA);                 /* 在途时销毁设备 */
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    pumpForMs(600);                              /* 让在途应答自然终结走孤儿路径 */
    CHECK_EQ(ctxA.n, 1, "detach 后旧应答不回调（回调计数不增）");

    /* —— 在途时销毁链路：静默位防应答（保证真正在途）→ deleteLater 链路 ——
     * 崩溃随本进程存活核查；泄漏随进程退出交 ASan 核查。 */
    ctrlSet("bus1_mute_u1");
    XObject* ownerB = XObject_create();
    CHECK(ownerB != NULL, "ownerB 创建");
    CHECK(XVfdModbusLink_submitRead(link, ownerB, 1, 4, 2, onLinkDone, ownerB,
                                    XModbusRequestPriority_Normal),
          "链路销毁前在途提交");
    CHECK_EQ(XVfdModbusLink_outstanding(link), 1, "链路在途=1");
    XObject_deleteLater((XObject*)link);         /* 在途时销毁链路 */
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    pumpForMs(1200);                             /* 客户端超时终结在途应答 */
    CHECK(true, "在途销毁链路后事件泵存活（零崩溃；泄漏交 ASan 退出核查）");
    ctrlClear("bus1_mute_u1");
    XObject_deleteLater(ownerB);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    /* —— 存活 sanity：client 仍可用。 —— */
    XModbusReply* s = sendReadHr((XModbusClient*)c, 1, 0, 1,
                                 XModbusRequestPriority_Normal);
    watchLate(7, s);
    CHECK(waitUntil(predWatchDoneOk, &g_watches[7], 2000), "事后 sanity 读应答");

    rtuClose(c);
    sceneEnd();
}

/* ==================== S9 串口双开守卫 ==================== */

static void testS9(void)
{
    sceneBegin("S9", "串口双开守卫（同实例二次 open 返回 false）");
    g_serialErrCnt = 0;
    XSerialPort* p1 = XSerialPort_create();
    CHECK(p1 != NULL, "串口对象创建");
    if (!p1) { sceneEnd(); return; }
    XObject_connect_2((XObject*)p1, XSignal(XSerialPort_errorOccurred_signal),
                      onSerialError);
    XSerialPort_setPortName(p1, g_pty1);
    CHECK(XSerialPort_open_base((XIODevice*)p1, XIODevice_ReadWrite), "首次 open 成功");
    CHECK(XSerialPort_isOpen((XIODevice*)p1), "端口已打开");
    const bool again = XSerialPort_open_base((XIODevice*)p1, XIODevice_ReadWrite);
    CHECK(!again, "同实例二次 open 返回 false（不静默重开覆盖 fd）");
    CHECK_EQ(XSerialPort_error(p1), XSerialPort_OpenError, "error=OpenError");
    CHECK_EQ(g_serialErrCnt, 0, "拒绝性双开不发射 errorOccurred");
    /* 观察项（不断言）：跨实例双开由平台语义决定（A8 守卫为实例级）。 */
    {
        XSerialPort* p2 = XSerialPort_create();
        if (p2) {
            XSerialPort_setPortName(p2, g_pty1);
            tlog("S9 观察: 跨实例二次 open 返回 %d（平台层未强制互斥）",
                 (int)XSerialPort_open_base((XIODevice*)p2, XIODevice_ReadWrite));
            XSerialPort_close_base((XIODevice*)p2);
            XClassDelete((XClass*)p2);
        }
    }
    XSerialPort_close_base((XIODevice*)p1);
    CHECK(!XSerialPort_isOpen((XIODevice*)p1), "close 后端口已关");
    XClassDelete((XClass*)p1);
    sceneEnd();
}

/* ==================== S10 微浸泡 ==================== */

static void testS10(void)
{
    sceneBegin("S10", "微浸泡（TCP 连接抖动 + RTU CRC 注入循环 50 轮，零崩溃）");
    XModbusTcpClient* tcp = XModbusTcpClient_create();
    XModbusRtuSerialClient* rtu = rtuMake(g_pty1, 200, 0, false);
    CHECK(tcp != NULL && rtu != NULL, "TCP/RTU 客户端创建");
    if (!tcp || !rtu) { rtuClose(rtu); sceneEnd(); return; }
    XModbusClient_setTimeout((XModbusClient*)tcp, 300);
    XModbusClient_setAutoReconnect((XModbusClient*)tcp, false);
    XModbusDevice_setConnectionParameter_ref((XModbusDevice*)tcp,
        XModbusDevice_NetworkAddressParameter, XVariant_create_utf8_str("127.0.0.1"));
    XModbusDevice_setConnectionParameter_ref((XModbusDevice*)tcp,
        XModbusDevice_NetworkPortParameter, XVariant_create_int(g_tcpPort));
    CHECK(rtuOpen(rtu), "RTU 客户端连接 bus1");

    const int rounds = 50;
    int okCnt = 0, crcErrCnt = 0, connFail = 0;
    for (int i = 0; i < rounds; ++i) {
        /* ① 连接抖动：TCP 连上→读→断开。 */
        if (!XModbusDevice_connectDevice((XModbusDevice*)tcp) ||
            !waitUntil(predConnected, tcp, 1000)) { ++connFail; }
        else {
            XModbusReply* r = sendReadHr((XModbusClient*)tcp, 1, 0, 2,
                                         XModbusRequestPriority_Normal);
            watchLate(0, r);
            if (waitUntil(predWatchDoneOk, &g_watches[0], 1000)) ++okCnt;
        }
        XModbusDevice_disconnectDevice((XModbusDevice*)tcp);
        waitUntil(predUnconnected, tcp, 500);
        /* ② CRC 注入：RTU 读坏 CRC 从站 → 应答 error 终结。 */
        ctrlSet("bus1_badcrc_u3");
        XModbusReply* cr = sendReadHr((XModbusClient*)rtu, 3, 0, 2,
                                      XModbusRequestPriority_Normal);
        watchLate(1, cr);
        if (waitUntil(predWatchDone, &g_watches[1], 1500) &&
            g_watches[1].lastError != (int)XModbusDevice_NoError) ++crcErrCnt;
        ctrlClear("bus1_badcrc_u3");
    }
    CHECK_EQ(connFail, 0, "50 轮 TCP 连接全部成功（零连接失败）");
    CHECK_EQ(okCnt, rounds, "50 轮 TCP 读全部应答");
    CHECK_EQ(crcErrCnt, rounds, "50 轮 CRC 注入全部以 error 终结");

    /* 终态 sanity：坏 CRC 撤除后 RTU 恢复正常应答。 */
    XModbusReply* fin = sendReadHr((XModbusClient*)rtu, 1, 0, 2,
                                   XModbusRequestPriority_Normal);
    watchLate(2, fin);
    CHECK(waitUntil(predWatchDone, &g_watches[2], 2000), "浸泡终态 sanity 读终结");
    if (!g_watches[2].finished) diagClient("S10 挂死", rtu, fin);
    tlog("S10 sanity 终态: finished=%d lastError=%d",
         g_watches[2].finished, g_watches[2].lastError);
    CHECK(waitUntil(predWatchDoneOk, &g_watches[2], 2000), "浸泡终态 sanity 读应答");

    rtuClose(rtu);
    XObject_deleteLater((XObject*)tcp);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    sceneEnd();
}

/* ==================== S11 未连接同步终态收口 ==================== */
/* 复核建议 (a)：断链（未连接）下经链路提交——submit 内部同步终态路径必须
 * 就地 done(false) 且不占在途登记；广播写不得因分支次序假报 ok。 */

static void testS11(void)
{
    sceneBegin("S11", "未连接同步终态收口（读/写/广播写同步 done(false) 且不占在途）");
    XModbusRtuSerialClient* c = rtuMake(g_pty1, 300, 0, false); /* 刻意不连接 */
    CHECK(c != NULL, "RTU 客户端创建（保持未连接）");
    XVfdModbusLink* link = XVfdModbusLink_create();
    CHECK(link != NULL && XVfdModbusLink_setClient(link, (XModbusClient*)c),
          "链路创建并绑定未连接客户端");
    if (!c || !link) { rtuClose(c); sceneEnd(); return; }
    static DoneCtx ctx;
    ctx.n = 0; ctx.lastOk = -1;
    XObject* owner = XObject_create();
    CHECK(owner != NULL, "owner 创建");

    CHECK(XVfdModbusLink_submitRead(link, owner, 1, 0, 2, onLinkDone, &ctx,
                                    XModbusRequestPriority_Normal),
          "未连接读受理");
    CHECK_EQ(ctx.n, 1, "读同步 done 恰好一次");
    CHECK_EQ(ctx.lastOk, 0, "读同步 done ok=false");
    CHECK_EQ(XVfdModbusLink_outstanding(link), 0, "读不占在途登记");

    ctx.n = 0;
    CHECK(XVfdModbusLink_submitWriteSingle(link, owner, 1, 0, 0x55, onLinkDone,
                                           &ctx, XModbusRequestPriority_Normal),
          "未连接写受理");
    CHECK_EQ(ctx.n, 1, "写同步 done 恰好一次");
    CHECK_EQ(ctx.lastOk, 0, "写同步 done ok=false");
    CHECK_EQ(XVfdModbusLink_outstanding(link), 0, "写不占在途登记");

    ctx.n = 0;
    CHECK(XVfdModbusLink_submitWriteSingle(link, owner, 0, 0, 0x55, onLinkDone,
                                           &ctx, XModbusRequestPriority_Normal),
          "未连接广播写受理");
    CHECK_EQ(ctx.n, 1, "广播写同步 done 恰好一次");
    CHECK_EQ(ctx.lastOk, 0, "断链广播写不假报 ok（终态检查先于广播分支）");
    CHECK_EQ(XVfdModbusLink_outstanding(link), 0, "广播写不占在途登记");

    XObject_deleteLater(owner);
    XObject_deleteLater((XObject*)link);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    rtuClose(c);
    sceneEnd();
}

/* ==================== S12 Hub 断链重连 ==================== */
/* 复核建议 (b)：断开→重连全周期——在途应答终结后 inflight 不得滞留
 * （滞留则单飞闸挡死续泵），重连后轮询必须恢复增长。 */

static struct { XModbusHub* hub; int id; } g_hubCtx12;
static bool predHubConnected12(void* ctx)
{
    (void)ctx;
    return XModbusHub_deviceState(g_hubCtx12.hub, g_hubCtx12.id)
           == XModbusDevice_ConnectedState;
}
static bool predHubUnconnected12(void* ctx)
{
    (void)ctx;
    return XModbusHub_deviceState(g_hubCtx12.hub, g_hubCtx12.id)
           == XModbusDevice_UnconnectedState;
}

static void testS12(void)
{
    sceneBegin("S12", "Hub 断链重连（在途终结不滞留 inflight→重连轮询恢复增长）");
    XModbusRtuSerialClient* c = rtuMake(g_pty1, 250, 0, true);
    CHECK(c != NULL, "RTU 客户端创建");
    XModbusHub* hub = XModbusHub_create();
    CHECK(hub != NULL, "hub 创建");
    if (!c || !hub) { rtuClose(c); sceneEnd(); return; }
    const int id = XModbusHub_addDevice(hub, (XModbusClient*)c, "rtu");
    CHECK(id > 0, "设备注入（所有权转移 Hub）");
    if (id <= 0) { XModbusHub_deleteLater((XObject*)hub); sceneEnd(); return; }
    XModbusMapping* map = XModbusMapping_create();
    XModbusMapping_addRange(map, XModbusInputRegisters, 0, 2, id, 1, 0);
    XModbusHub_setMapping(hub, map);
    CHECK(XModbusHub_setPollInterval(hub, id, 100), "轮询拍 100ms");
    XModbusHub_startPolling(hub);

    g_hubCtx12.hub = hub; g_hubCtx12.id = id;
    CHECK(XModbusHub_connectAll(hub), "connectAll 连接");
    CHECK(waitUntil(predHubConnected12, hub, 2000), "设备进入 Connected");
    PredOkGrow grow = { hub, id, XModbusHub_pollOkCount(hub, id) };
    CHECK(waitUntil(predPollOkGrew, &grow, 2000), "在线基线 pollOk 增长");

    /* 断开（用户口径）：在途应答终结→收口槽清 inflight。Hub 泵有连接态
     * 门槛（断链期不提交），滞留风险在重连侧：若同步终态/断链清场路径
     * 滞留 inflight，单飞闸会挡死续泵——重连后 pollOk 恢复增长即守卫。 */
    XModbusDevice_disconnectDevice((XModbusDevice*)c);
    CHECK(waitUntil(predHubUnconnected12, hub, 2000), "断开进入 Unconnected");
    pumpForMs(300);
    CHECK(XModbusHub_connectAll(hub), "重新连接发起");
    CHECK(waitUntil(predHubConnected12, hub, 3000), "重新进入 Connected");
    grow.base = XModbusHub_pollOkCount(hub, id);
    CHECK(waitUntil(predPollOkGrew, &grow, 2500),
          "重连后 pollOk 恢复增长（inflight 无滞留）");

    hubShutdown(hub);
    sceneEnd();
}

/* ==================== S13 设备层断链续拍 ==================== */
/* 复核建议 (c)+终审 blocking 回归：设备级单飞在断链拍不得被同步收口路径
 * 复置忙（PollDue 先置忙次序根修）——断链多拍后不忙、链路无滞留，重连后
 * 轮询解析恢复增长。 */

static int g_parseCnt13;
static void onParse13(XObject* owner, const XModbusDataUnit* result)
{
    (void)owner; (void)result;
    ++g_parseCnt13;
}
static bool predParse13(void* ctx) { return g_parseCnt13 >= (int)(intptr_t)ctx; }

static void testS13(void)
{
    sceneBegin("S13", "设备层断链续拍（断链 PollDue 不卡死→静默退避→重连轮询恢复）");
    g_parseCnt13 = 0;
    XModbusRtuSerialClient* c = rtuMake(g_pty1, 300, 0, false);
    CHECK(c != NULL && rtuOpen(c), "bus1 连接");
    if (!c) { sceneEnd(); return; }
    XVfdModbusLink* link = XVfdModbusLink_create();
    CHECK(link != NULL && XVfdModbusLink_setClient(link, (XModbusClient*)c),
          "链路创建并绑定客户端");
    XVfdModbusBase* dev = XVfdModbusBase_create();
    CHECK(dev != NULL, "Modbus 语义设备创建（骨架直建，测试轮询引擎）");
    if (!link || !dev) {
        if (link) XObject_deleteLater((XObject*)link);
        if (dev) XObject_deleteLater((XObject*)dev);
        rtuClose(c); sceneEnd(); return;
    }
    CHECK(XVfdModbusBase_setLink(dev, link, 1), "设备绑定链路(unit1)→登记轮询计划");
    CHECK(XVfdModbusBase_addPollGroup(dev, 0, 2, (uint32_t)XVfdPollItem_Status,
                                      onParse13) >= 0,
          "注册轮询读组（addr=0 count=2 Status）");
    XVfdLink_setBackoff(link, 2, 1000); /* 2 次失败即静默 1s，缩短观测窗 */
    CHECK(waitUntil(predParse13, (void*)1, 2000), "在线基线：轮询解析到达");

    /* 断链：只断开（不销毁客户端）。此后每拍 PollDue 提交同步终态、done
     * 就地清忙——设备不得被复置忙卡死；失败记账进静默退避。 */
    XModbusDevice_disconnectDevice((XModbusDevice*)c);
    CHECK(waitUntil(predUnconnected, c, 1000), "断开串口（进入 Unconnected）");
    pumpForMs(1200);
    CHECK(!XVfdDevice_isBusy(&dev->m_base), "断链多拍后设备不忙（单飞无卡死）");
    CHECK(!XVfdModbusLink_isBusy(link), "断链多拍后链路无滞留在途");

    /* 重连：静默到期探活成功→轮询恢复（解析计数继续增长）。 */
    CHECK(rtuOpen(c), "重开串口");
    CHECK(waitUntil(predParse13, (void*)(intptr_t)(g_parseCnt13 + 1), 4000),
          "重连后轮询恢复（解析继续增长）");

    XVfdLink_stopPolling(link);
    XVfdModbusBase_detach(dev);
    XObject_deleteLater((XObject*)dev);
    XObject_deleteLater((XObject*)link);
    rtuClose(c);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    sceneEnd();
}

/* ==================== 主流程 ==================== */

static bool readPathFile(const char* path, char* out, size_t cap)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    const bool ok = fgets(out, (int)cap, f) != NULL;
    fclose(f);
    if (ok) out[strcspn(out, "\r\n")] = 0;
    return ok && out[0] != 0;
}

int main(int argc, char* argv[])
{
    g_bootMs = XDateTime_currentMSecsSinceEpoch();
    if (argc > 1 && argv[1][0]) {
        snprintf(g_base, sizeof(g_base), "%s", argv[1]);
    } else {
        const char* env = getenv("CAMPAIGN_BASE");
        if (env && env[0]) snprintf(g_base, sizeof(g_base), "%s", env);
    }
    if (argc > 2) g_tcpPort = atoi(argv[2]);

    printf("=== 战役 1028 XProtocol 端到端联调测试 ===\n");
    printf("base=%s tcp=%d\n", g_base, g_tcpPort); fflush(stdout);

    XCoreApplication_create(0, NULL);

    /* 就绪等待：三条 pty 路径（驱动已预检，这里兜底轮询 5s）。 */
    bool pathsOk = false;
    {
        const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 5000;
        for (;;) {
            char p[512];
            snprintf(p, sizeof(p), "%s/pty1_path", g_base);
            const bool a = readPathFile(p, g_pty1, sizeof(g_pty1));
            snprintf(p, sizeof(p), "%s/pty2_path", g_base);
            const bool b = readPathFile(p, g_pty2, sizeof(g_pty2));
            snprintf(p, sizeof(p), "%s/pty3_path", g_base);
            const bool c3 = readPathFile(p, g_pty3, sizeof(g_pty3));
            if (a && b && c3) { pathsOk = true; break; }
            if (XDateTime_currentMSecsSinceEpoch() >= deadline) break;
            pumpForMs(50);
        }
    }
    CHECK(pathsOk, "三条 pty 路径就绪");
    if (!pathsOk) {
        printf("=== 结果: 从站未就绪, 失败 %d 项 ===\n", g_fail); fflush(stdout);
        return g_fail;
    }

    testS1();
    testS2();
    testS3();
    testS6();
    testS5();
    testS7();
    testS8();
    testS4();
    testS9();
    testS11();
    testS12();
    testS13();
    testS10();

    XCoreApplication_processEvents(XEventLoop_AllEvents);
    printf("=== 结果: 失败 %d 项 (S1..S13 全矩阵) ===\n", g_fail);
    fflush(stdout);
    return g_fail;
}
