/**
 * @file       xfdv_m512_rtu_test.c
 * @brief      511M/512M/513M **真机** Modbus-RTU 闭环探针（独立可执行 XModbusM512RtuProbe）。
 * @details    [2026-11-03 真机联调] 本探针用 XinYueC 框架**自己的** Modbus RTU 栈
 *             （XModbusRtuSerialClient → XModbusHub → XModbusLink → XVfdM512）
 *             连**真实**的 512M 变频器（CH343 USB-485，COM17 @115200 8N1，
 *             从站地址 2，电机未接）。
 *
 *             与 Test/XProtocolTest/xfdv_m512_test.c 的**关键差别**：那份的
 *             「从站」是进程内 TCP 仿真器（xvfd_slave_emu.h）；本探针**不用**
 *             仿真器，从站就是真变频器，设备层闭环照样走
 *             xvfd_harness.h 脚手架（Hub + 通道组 + 语义点 + 从站约束）。
 *
 *             分层：
 *               第 1 层「框架 RTU 栈直连真机」（绕开设备层）：
 *                 FC03 读 0x3000（期望 3=停机）/ 0x8000（期望 0=无故障）/
 *                 0xF00A（期望 500=P0.10 50.0Hz）；
 *                 FC06 写 0x1000=2000 → 回读 0x1000 期望 2000。
 *               第 2 层「设备层闭环」（XVfdM512 + Hub + 脚手架，单站）：
 *                 命令源/频率源切换（0xF002/0xF003）→ 遥测 → 调频 10Hz
 *                 （0x1000 期望 2000）→ 正转（0x3000 期望 1）→ 反转
 *                 （0x3000 期望 2）→ 减速停机（0x3000 期望 3）。
 *
 *             安全：电机未接，**退出前必发自由停机（0x2000=5）**并回读 0x3000
 *             确认停机——cleanup 路径无条件执行（中途失败也不例外）。
 *
 *             ⚠ 实机坑（已在本文件处理，详见交付报告）：
 *               ① 变频器错误应答帧是 **8 字节且功能码不置 0x80**（非标准），
 *                  框架 RTU 解析不了 ⇒ 本探针**不主动发非法请求**；
 *                  但设备层 Hub 会轮询「只写组」（0x2000 命令字组 / 0x1F00
 *                  密码组），真机对这两组**必然回错误帧**；该帧会毒化客户端
 *                  接收缓冲（框架不清理），只能靠「超时后重试」自愈 ⇒ 客户端
 *                  超时收紧到 400ms、重试保留 1 次（0 次重试 ⇒ 缓冲永不清 ⇒
 *                  客户端彻底死掉）。
 *               ② M512 通信组多（28 组），一轮轮转数百 ms ⇒ 设备层动作监督
 *                  超时用 XVfdDevice_setActionTimeout(dev, 8000) 放宽。
 *               ③ 减速时间 P0.18 = 100×0.1s = **10 秒** ⇒ 减速停机后必须等
 *                  13 秒才真的停机（本探针按此等待，不按 2 秒断言）。
 *               ④ 加速时间 P0.17 = 10 秒 ⇒ 启动后 3 秒时输出频率还在爬坡，
 *                  本探针先打印 3 秒时的实测值（INFO），再等 13 秒断言
 *                  0x1001 ≈ 100（10.0Hz）。
 *               ⑤ **框架缺陷（只报告，未改框架）**：XVfdDevice 的状态定时器
 *                  每次触发都泄漏一个定时器 fd——VXVfdDevice_timerEvent 里把
 *                  m_statusTimer 置成 XTIMER_INVALID_ID 却**没有**
 *                  XObject_killTimer，旧定时器仍挂在全局时间轮上重复触发，
 *                  其 fd 永不释放（Src/Industry/XVfd/XVfdDevice.c）。fd 表只有
 *                  128 项：状态间隔 200ms 时约 25 秒占满 ⇒ 之后任何新定时器
 *                  注册失败（RTU 客户端帧间定时器 id 变 -1）⇒ **完整应答帧滞留
 *                  在接收缓冲永不被解析** ⇒ 一切请求超时。本探针把状态间隔放宽
 *                  到 1000ms 绕开该缺陷，并在结尾打印 fd 表占用作为量化证据。
 *
 *             用法：./XModbusM512RtuProbe [--port COM17] [--unit 2]
 *             退出码=失败用例数（0 = 全 PASS）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XDateTime.h"
#include "XModbusClient.h"
#include "XModbusDevice.h"
#include "XModbusReply.h"
#include "XModbusDataUnit.h"
#include "XModbusRtuSerialClient.h"
#include "XSerialPort.h"
#include "XByteArray.h"
#include "XFileDescriptor.h"
#include "XVariant.h"
#include "XString.h"
#include "xvfd_harness.h"
#include "XVfd/XVfdM512.h"
#include "XVfd/XVfdM512_Rtu.h"
#include "XVfd/XVfdDevice.h"

/* ==================== 关键地址（手册 4 位十六进制地址，实机标定） ==================== */
#define M512_ADDR_FREQ_SET     0x1000  /**< 通讯频率给定（写）：±10000 = ±100.00% */
#define M512_ADDR_OUT_FREQ     0x1001  /**< 运行频率（0.1Hz 定点：100 = 10.0Hz） */
#define M512_ADDR_BUS_VOLTAGE  0x1002  /**< 母线电压 */
#define M512_ADDR_OUT_CURRENT  0x1004  /**< 输出电流 */
#define M512_ADDR_CMD_WORD     0x2000  /**< 控制命令（只写）：1正转 2反转 5自由停 6减速停 */
#define M512_ADDR_RUN_STATE    0x3000  /**< 运行状态：1正转 2反转 3停机 */
#define M512_ADDR_FAULT        0x8000  /**< 故障码 */
#define M512_ADDR_CMD_SOURCE   0xF002  /**< P0.02 命令源（2=通讯） */
#define M512_ADDR_FREQ_SOURCE  0xF003  /**< P0.03 主频率源（9=通讯给定） */
#define M512_ADDR_MAX_FREQ     0xF00A  /**< P0.10 最大频率（0.1Hz 定点：500 = 50.0Hz） */

/* ==================== 运行参数 ==================== */
#define CLIENT_TIMEOUT_MS      400     /**< 单帧超时（真机应答 ~5ms；取值小是为了压住错误帧代价） */
#define CLIENT_RETRIES         1       /**< 重试次数（必须 ≥1：靠重试清接收缓冲，见头注①） */
#define RAW_WAIT_MS            6000    /**< 原始读等待上限（设备层轮询期间要排队） */
#define RAW_MAX_ATTEMPTS       3       /**< 原始读的应用层重试次数（框架缓冲残留兜底） */
#define ACTION_TIMEOUT_MS      8000    /**< 设备层动作监督超时（迁就 28 组轮转周期） */
#define RAMP_WAIT_MS           13000   /**< 加速/减速等待（P0.17/P0.18 = 100×0.1s = 10s + 余量） */
/* 状态快照间隔：默认取 1000ms（不是 100/200ms）。
 * 原因（实机取证到的框架缺陷）：XVfdDevice 的状态定时器**每次触发都泄漏一个
 * 定时器 fd**——定时器回调里把 m_statusTimer 置成 XTIMER_INVALID_ID 却**没有**
 * XObject_killTimer（Src/Industry/XVfd/XVfdDevice.c 的 VXVfdDevice_timerEvent），
 * 旧定时器仍挂在全局时间轮上重复触发，其 fd 永远不被释放。fd 表只有 128 项，
 * 状态间隔越小泄漏越快：200ms 时约 25 秒就把表占满 ⇒ 之后任何新定时器注册
 * 失败（RTU 客户端的帧间定时器 id 变成 -1）⇒ 完整应答帧滞留在接收缓冲永不被
 * 解析 ⇒ 请求一律超时。1000ms 时本轮（约 45 秒）只泄漏 ~45 个 fd，够用。
 * 结尾会打印 fd 表占用作为该缺陷的量化证据。 */
/* 状态发布间隔（毫秒）。**缺省 200ms ＝ 泄漏复现配置**：
 * 修复前每拍泄漏一个定时器 fd，200ms 间隔约 25s 就能占满 128 槽的 fd 表。
 * 回归锁正是在这个配置下断言 fd 不再线性增长，故缺省不用 1000ms 绕开。 */
#define STATUS_INTERVAL_MS     200

/** 挂载完成时的定时器 fd 计数（回归锁基线）。 */
static int g_fdTimersAtMount = 0;

static int g_fail = 0;
static const char* g_case = "";
static int64_t g_bootMs = 0;
/* 用**品牌层** RTU 客户端：本厂非标准错误帧由 XVfdM512RtuClient 归一化，
 * 框架的 XModbusRtuSerialClient 不含任何厂商特例。 */
static XModbusRtuSerialClient* g_client = NULL;
static int g_unitId = 2;
static char g_port[64] = "COM17";
static XModbusHub* g_hub = NULL;        /**< 设备层 Hub（诊断用：定时器 fd 归属） */
static XObject* g_devObj = NULL;        /**< 设备对象（诊断用：定时器 fd 归属） */
static XModbusLink* g_hubStationLink = NULL; /**< 本站链路（Hub 缓存回读用） */

/* 设备层动作落定/错误计数（用于区分「设备层未落定」与「实际值正确」） */
typedef struct {
    int doneCount;
    int lastOp;
    bool lastOk;
    int errorCount;
    char lastError[192];
    int paramReadCount;
    uint16_t paramReadId;
    int32_t paramReadRaw;
    bool paramReadOk;
} DevCounters;

static DevCounters g_dev;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", (msg)); fflush(stdout); } \
    else { printf("  [FAIL] %s (%s@%d)\n", (msg), g_case, __LINE__); fflush(stdout); ++g_fail; } \
} while (0)

#define CHECKF(cond, ...) do { \
    if (cond) { printf("  [OK] "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } \
    else { printf("  [FAIL] "); printf(__VA_ARGS__); \
           printf("   <-- 失败点 %s@%d\n", g_case, __LINE__); fflush(stdout); ++g_fail; } \
} while (0)

#define INFOF(...) do { printf("  [INFO] "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

static void tlog(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("[%8.3fs] ", (double)(XDateTime_currentMSecsSinceEpoch() - g_bootMs) / 1000.0);
    vprintf(fmt, ap); printf("\n"); fflush(stdout);
}

/** @brief 泵事件指定毫秒（单线程事件循环；串口应答靠事件循环派发）。 */
static void pump(int ms)
{
    const int64_t deadline = XDateTime_currentMSecsSinceEpoch() + (int64_t)ms;
    while (XDateTime_currentMSecsSinceEpoch() < deadline) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        const int64_t spin = XDateTime_currentMSecsSinceEpoch() + 1;
        while (XDateTime_currentMSecsSinceEpoch() < spin) { }
    }
}

/* ==================== 第 1 层：框架 RTU 栈直连真机 ==================== */

/**
 * @brief 原始读一��保持寄存器（FC03，单寄存器）。
 * @param c 客户端。
 * @param unit 从站地址。
 * @param addr 远端地址。
 * @param outValue 输出值。
 * @param timeoutMs 最长等待毫秒。
 * @return 成功返回 true。
 */
/**
 * @brief 打印 RTU 客户端接收缓冲的残留字节与两个关键定时器 id（框架问题取证用）。
 * @param c RTU 客户端（可为 NULL）。
 * @details 帧间定时器 id 与请求超时定时器 id 若相同，帧间回调会被误派发成
 *          「请求超时」⇒ 缓冲里的完整应答帧永远不被解析（框架层缺陷取证点）。
 */
static void dumpRxResidue(XModbusRtuSerialClient* c)
{
    const unsigned char* p;
    size_t n;
    size_t i;
    int used = 0;
    int timers = 0;
    int k;
    if (!c) return;
    printf("  [dbg] 定时器 id：帧间=%d 请求超时=%d（相同即帧间回调被误派发）"
           " 重试计数=%d 当前在途=%s\n",
           (int)c->m_interFrameTimer,
           (int)((XModbusClient*)c)->m_timeoutTimer,
           (int)c->m_retryCount,
           c->m_currentReply ? "有" : "无");
    for (k = 0; k < XFD_TABLE_SIZE; ++k) {
        XFileDescriptor* d = XFd_get((XFd)k);
        if (!d) continue;
        ++used;
        if (XFd_type((XFd)k) == XFD_TYPE_TIMER) ++timers;
    }
    printf("  [dbg] fd 表占用 %d/%d（其中定时器 %d）\n", used, (int)XFD_TABLE_SIZE, timers);
    /* 归属统计：把定时器 fd 按属主分组（定位是谁在泄漏定时器 fd）。 */
    {
        int nClient = 0, nPort = 0, nHub = 0, nDev = 0, nOther = 0;
        void* pClient = (void*)g_client;
        void* pPort = (void*)XModbusRtuSerialClient_serialPort(g_client);
        void* pHub = (void*)g_hub;
        void* pDev = (void*)g_devObj;
        for (k = 0; k < XFD_TABLE_SIZE; ++k) {
            XFileDescriptor* d = XFd_get((XFd)k);
            if (!d || XFd_type((XFd)k) != XFD_TYPE_TIMER) continue;
            if (d->object == pClient) ++nClient;
            else if (d->object == pPort) ++nPort;
            else if (d->object == pHub) ++nHub;
            else if (d->object == pDev) ++nDev;
            else ++nOther;
        }
        printf("  [dbg] 定时器 fd 归属：RTU客户端=%d 串口=%d Hub=%d 设备=%d 其他=%d\n",
               nClient, nPort, nHub, nDev, nOther);
    }
    fflush(stdout);
    if (!c->m_receiveBuffer) return;
    n = (size_t)XByteArray_size_base((const XContainer*)c->m_receiveBuffer);
    if (n == 0) {
        printf("  [dbg] 接收缓冲残留 0 字节（应答根本没进缓冲）\n");
        fflush(stdout);
        return;
    }
    p = (const unsigned char*)XContainerDataAddr((const XContainer*)c->m_receiveBuffer);
    printf("  [dbg] 接收缓冲残留 %u 字节：", (unsigned)n);
    for (i = 0; i < n && i < 24; ++i) printf(" %02X", p[i]);
    printf("\n");
    fflush(stdout);
}

static bool rawReadOnce(XModbusClient* c, int unit, uint16_t addr, uint16_t* outValue,
                        int timeoutMs)
{
    XModbusDataUnit data;
    XModbusReply* reply;
    int64_t deadline;
    bool ok = false;

    if (!c || !outValue) return false;
    XModbusDataUnit_init_ex(&data, XModbusHoldingRegisters, addr, 1);
    reply = XModbusClient_sendReadRequest(c, &data, unit, XModbusRequestPriority_Normal);
    if (!reply) {
        printf("  [dbg] rawRead 0x%04X：sendReadRequest 返回 NULL（客户端忙/未连接）\n", addr);
        fflush(stdout);
        return false;
    }
    deadline = XDateTime_currentMSecsSinceEpoch() + (int64_t)timeoutMs;
    while (!XModbusReply_isFinished(reply)) {
        pump(2);
        if (XDateTime_currentMSecsSinceEpoch() >= deadline) break;
    }
    if (XModbusReply_isFinished(reply)) {
        if (XModbusReply_error(reply) == XModbusDevice_NoError) {
            const XModbusDataUnit* res = XModbusReply_result_const(reply);
            if (res && XModbusDataUnit_valueCount(res) >= 1) {
                *outValue = XModbusDataUnit_value(res, 0);
                ok = true;
            }
        } else {
            printf("  [dbg] rawRead 0x%04X 失败：error=%d（%s）接收缓冲残留=%u 字节 "
                   "设备态=%d 设备错误=%d\n",
                   addr, (int)XModbusReply_error(reply),
                   (XModbusReply_error(reply) == XModbusDevice_TimeoutError)
                       ? "TimeoutError" : "其它错误",
                   g_client ? (unsigned)XByteArray_size_base((const XContainer*)g_client->m_receiveBuffer) : 0u,
                   g_client ? (int)XModbusDevice_state((XModbusDevice*)g_client) : -1,
                   g_client ? (int)XModbusDevice_error((XModbusDevice*)g_client) : -1);
            fflush(stdout);
            dumpRxResidue(g_client);
        }
    } else {
        printf("  [dbg] rawRead 0x%04X 超时（%dms 内未收口）接收缓冲残留=%u 字节\n",
               addr, timeoutMs,
               g_client ? (unsigned)XByteArray_size_base((const XContainer*)g_client->m_receiveBuffer) : 0u);
        fflush(stdout);
    }
    /* 未收口时不释放：客户端仍持有在途引用，强行释放会踩空指针。 */
    if (XModbusReply_isFinished(reply)) XObject_deleteLater((XObject*)reply);
    pump(2);
    return ok;
}

/**
 * @brief 原始读**多个**保持寄存器（FC03，count 个）；返回框架判定的错误码。
 * @param c 客户端。
 * @param unit 从站地址。
 * @param addr 远端起始地址。
 * @param count 读点数（>=1）。
 * @param timeoutMs 最长等待毫秒。
 * @param outError 输出：XModbusReply_error() 的结果（NoError 表示读成功）。
 * @param outElapsed 输出：实际耗时毫秒。
 * @return 请求是否在超时内**收口**（收口 ≠ 成功：失败也要算收口）。
 * @note 真机对「单点地址连读」「越界地址」回**本厂非标准错误帧**
 *       （8 字节、功能码不置 0x80）。通用 RTU 客户端算不出它的帧长 ⇒
 *       帧留缓冲 ⇒ 请求超时；只有品牌层客户端（XVfdM512RtuClient）能认领。
 */
static bool rawReadCount(XModbusClient* c, int unit, uint16_t addr, uint16_t count,
                         int timeoutMs, int* outError, int64_t* outElapsed)
{
    XModbusDataUnit data;
    XModbusReply* reply;
    int64_t deadline;
    int64_t t0;

    if (outError) *outError = XModbusDevice_TimeoutError;
    if (!c) return false;
    XModbusDataUnit_init_ex(&data, XModbusHoldingRegisters, addr, count);
    t0 = XDateTime_currentMSecsSinceEpoch();
    reply = XModbusClient_sendReadRequest(c, &data, unit, XModbusRequestPriority_Normal);
    if (!reply) return false;
    deadline = t0 + (int64_t)timeoutMs;
    while (!XModbusReply_isFinished(reply)) {
        pump(2);
        if (XDateTime_currentMSecsSinceEpoch() >= deadline) break;
    }
    if (outElapsed) *outElapsed = XDateTime_currentMSecsSinceEpoch() - t0;
    if (!XModbusReply_isFinished(reply)) return false;   /* 没收口（挂死） */
    if (outError) *outError = (int)XModbusReply_error(reply);
    return true;
}

/**
 * @brief 原始读一个保持寄存器（FC03，单寄存器）；带应用层重试。
 * @details 框架 RTU 客户端在收到**非标准错误帧**（真机的 8 字节异常帧）后，
 *          接收缓冲不会被清空（框架只在「重试」时清），该残留字节会让下一帧
 *          的长度判定失效 ⇒ 偶发一次 TimeoutError。此处做应用层重试兜底，
 *          并把残留字节数打印出来作为框架问题的证据。
 */
static bool rawRead(XModbusClient* c, int unit, uint16_t addr, uint16_t* outValue,
                    int timeoutMs)
{
    int attempt;
    for (attempt = 1; attempt <= RAW_MAX_ATTEMPTS; ++attempt) {
        if (rawReadOnce(c, unit, addr, outValue, timeoutMs)) return true;
        if (attempt < RAW_MAX_ATTEMPTS) {
            printf("  [dbg] rawRead 0x%04X 应用层重试（第 %d 次）\n", addr, attempt + 1);
            fflush(stdout);
            pump(300);
        }
    }
    return false;
}

/**
 * @brief 原始写一个保持寄存器（FC06，单寄存器）。
 * @param c 客户端。
 * @param unit 从站地址。
 * @param addr 远端地址。
 * @param value 值。
 * @param timeoutMs 最长等待毫秒。
 * @return 成功返回 true。
 */
static bool rawWriteOnce(XModbusClient* c, int unit, uint16_t addr, uint16_t value,
                         int timeoutMs)
{
    XModbusDataUnit data;
    XModbusReply* reply;
    int64_t deadline;
    bool ok = false;

    if (!c) return false;
    XModbusDataUnit_init_ex(&data, XModbusHoldingRegisters, addr, 1);
    XModbusDataUnit_setValue(&data, 0, value);
    reply = XModbusClient_sendWriteRequest(c, &data, unit, XModbusRequestPriority_High);
    if (!reply) {
        printf("  [dbg] rawWrite 0x%04X=%u：sendWriteRequest 返回 NULL\n", addr,
               (unsigned)value);
        fflush(stdout);
        return false;
    }
    deadline = XDateTime_currentMSecsSinceEpoch() + (int64_t)timeoutMs;
    while (!XModbusReply_isFinished(reply)) {
        pump(2);
        if (XDateTime_currentMSecsSinceEpoch() >= deadline) break;
    }
    if (XModbusReply_isFinished(reply)) {
        if (XModbusReply_error(reply) == XModbusDevice_NoError) {
            ok = true;
        } else {
            printf("  [dbg] rawWrite 0x%04X=%u 失败：error=%d 接收缓冲残留=%u 字节\n",
                   addr, (unsigned)value, (int)XModbusReply_error(reply),
                   g_client ? (unsigned)XByteArray_size_base((const XContainer*)g_client->m_receiveBuffer) : 0u);
            fflush(stdout);
        }
    } else {
        printf("  [dbg] rawWrite 0x%04X=%u 超时（%dms 内未收口）\n", addr,
               (unsigned)value, timeoutMs);
        fflush(stdout);
    }
    if (XModbusReply_isFinished(reply)) XObject_deleteLater((XObject*)reply);
    pump(2);
    return ok;
}

/** @brief 原始写一个保持寄存器（FC06，单寄存器）；带应用层重试。 */
static bool rawWrite(XModbusClient* c, int unit, uint16_t addr, uint16_t value,
                     int timeoutMs)
{
    int attempt;
    for (attempt = 1; attempt <= RAW_MAX_ATTEMPTS; ++attempt) {
        if (rawWriteOnce(c, unit, addr, value, timeoutMs)) return true;
        if (attempt < RAW_MAX_ATTEMPTS) {
            printf("  [dbg] rawWrite 0x%04X=%u 应用层重试（第 %d 次）\n", addr,
                   (unsigned)value, attempt + 1);
            fflush(stdout);
            pump(300);
        }
    }
    return false;
}

/**
 * @brief 从设备层 Hub 缓存回读某语义槽的原始值（原始读失败时的第二证据源）。
 * @param role 语义角色。
 * @param outRaw 输出原始寄存器值。
 * @return 取到返回 true。
 * @note 走的是**同一条链路**（Hub 轮询回来的缓存），因此它证明的是
 *       「设备层确实看到了这个值」，与原始读互为印证。
 */
static bool hubReadRole(XVfdPointRole role, uint16_t* outRaw)
{
    XModbusLink* link = NULL;
    int area = 0;
    uint16_t addr = 0;
    if (!g_hub || !outRaw) return false;
    if (g_hubStationLink) link = g_hubStationLink;
    if (!link) return false;
    if (!XModbusLink_queryBinding(link, (uint16_t)role, &area, &addr)) return false;
    return XModbusHub_value(g_hub, XModbusLink_hubAreaType(area), addr, outRaw);
}

/* ==================== 设备层回调 ==================== */

static void onCommandCompleted(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    ++g_dev.doneCount;
    g_dev.lastOp = (int)XVarList_arg(args, XVfdOperation);
    g_dev.lastOk = XVarList_arg(args, bool);
    tlog("  设备层动作落定：op=%d ok=%d", g_dev.lastOp, (int)g_dev.lastOk);
}

static void onParamReadDone(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    ++g_dev.paramReadCount;
    g_dev.paramReadId = (uint16_t)XVarList_arg(args, uint16_t);
    g_dev.paramReadRaw = (int32_t)XVarList_arg(args, int32_t);
    g_dev.paramReadOk = XVarList_arg(args, bool);
}

static void onErrorOccurred(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    ++g_dev.errorCount;
    {
        const char* msg = XVarList_arg(args, const char*);
        if (msg) {
            snprintf(g_dev.lastError, sizeof(g_dev.lastError), "%s", msg);
            tlog("  设备层错误：%s", msg);
        }
    }
}

/**
 * @brief 等一次设备层动作落定（commandCompleted 信号）。
 * @param timeoutMs 最长等待毫秒。
 * @param outOk 输出动作结果（可为 NULL）。
 * @return 落定返回 true；超时返回 false。
 */
static bool waitActionDone(int timeoutMs, bool* outOk)
{
    const int start = g_dev.doneCount;
    const int64_t deadline = XDateTime_currentMSecsSinceEpoch() + (int64_t)timeoutMs;
    while (g_dev.doneCount == start) {
        pump(20);
        if (XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    }
    if (outOk) *outOk = g_dev.lastOk;
    return true;
}

/**
 * @brief 等设备空闲（无在途动作）。
 * @param dev 设备。
 * @param timeoutMs 最长等待毫秒。
 * @return 空闲返回 true；超时返回 false。
 */
static bool waitIdle(XVfdDevice* dev, int timeoutMs)
{
    const int64_t deadline = XDateTime_currentMSecsSinceEpoch() + (int64_t)timeoutMs;
    while (XVfdDevice_isBusy(dev)) {
        pump(20);
        if (XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    }
    return true;
}

/* ==================== 安全停机（退出前必做） ==================== */

/**
 * @brief 安全停机：自由停机（0x2000=5）并回读 0x3000 确认停机。
 */
static void safetyStop(void)
{
    uint16_t state = 0xFFFF;
    if (!g_client) return;
    if (XModbusDevice_state((XModbusDevice*)g_client) != XModbusDevice_ConnectedState) {
        printf("  [WARN] 客户端未连接，跳过安全停机（未能确��停机！）\n");
        fflush(stdout);
        ++g_fail;
        return;
    }
    tlog("安全停机：FC06 写 0x2000=5（自由停机）");
    if (!rawWrite((XModbusClient*)g_client, g_unitId, M512_ADDR_CMD_WORD, 5, RAW_WAIT_MS)) {
        printf("  [FAIL] 自由停机写帧未成功应答\n"); fflush(stdout); ++g_fail;
    }
    pump(1200);
    if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_RUN_STATE, &state, RAW_WAIT_MS)) {
        printf("  最终状态 = %u（期望 3=停机）%s\n", (unsigned)state,
               (state == 3) ? "" : "  <-- 未停机！");
        fflush(stdout);
        if (state != 3) ++g_fail;
    } else {
        printf("  [FAIL] 最终状态读取失败（未能确认停机）\n"); fflush(stdout); ++g_fail;
    }
}

/* ==================== main ==================== */

int main(int argc, char* argv[])
{
    XvfdHarness harness;
    XVfdM512* dev = NULL;
    const XVfdPointSpec* specs = NULL;
    const XVfdParamDesc* params = NULL;
    const XVfdCommGroup* groups = NULL;
    const XVfdUnitProtocol* protocol = NULL;
    const XVfdBindTable* bind = NULL;
    int specCount = 0;
    int paramCount = 0;
    int groupCount = 0;
    int devId = -1;
    int idMaxFreq = -1;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            snprintf(g_port, sizeof(g_port), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--unit") == 0 && i + 1 < argc) {
            g_unitId = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("用法: XModbusM512RtuProbe [--port COM17] [--unit 2]\n");
            return 0;
        }
    }

    memset(&harness, 0, sizeof(harness));
    memset(&g_dev, 0, sizeof(g_dev));
    g_bootMs = XDateTime_currentMSecsSinceEpoch();

    printf("=== 511M/512M/513M 真机 Modbus-RTU 闭环探针 ===\n");
    printf("串口=%s 115200 8N1 从站地址=%d（CH343 USB-485，电机未接）\n", g_port, g_unitId);
    fflush(stdout);

    XCoreApplication_create(0, NULL);

    /* ---------- 建客户端 + 开串口 ---------- */
    g_case = "connect";
    g_client = (XModbusRtuSerialClient*)XVfdM512RtuClient_create();
    if (!g_client) {
        printf("  [FAIL] RTU 客户端创建失败\n"); fflush(stdout);
        return 2;
    }
    /* 串口参数走 XModbusDevice 连接参数表：RTU 的 open() 会按表物化 XSerialPort。
     * （不能用 XModbusRtuSerialClient_serialPort() 直接设——串口对象是在
     *  open() 里才创建的，create() 之后该访问器返回 NULL。） */
    {
        XVariant* v;
        v = XVariant_create_utf8_str(g_port);
        XModbusDevice_setConnectionParameter((XModbusDevice*)g_client,
            XModbusDevice_SerialPortNameParameter, v);
        XClassDelete((XClass*)v);
        v = XVariant_create_int((int)XSerialPort_Baud115200);
        XModbusDevice_setConnectionParameter((XModbusDevice*)g_client,
            XModbusDevice_SerialBaudRateParameter, v);
        XClassDelete((XClass*)v);
        v = XVariant_create_int((int)XSerialPort_Data8);
        XModbusDevice_setConnectionParameter((XModbusDevice*)g_client,
            XModbusDevice_SerialDataBitsParameter, v);
        XClassDelete((XClass*)v);
        v = XVariant_create_int((int)XSerialPort_NoParity);
        XModbusDevice_setConnectionParameter((XModbusDevice*)g_client,
            XModbusDevice_SerialParityParameter, v);
        XClassDelete((XClass*)v);
        v = XVariant_create_int((int)XSerialPort_OneStop);
        XModbusDevice_setConnectionParameter((XModbusDevice*)g_client,
            XModbusDevice_SerialStopBitsParameter, v);
        XClassDelete((XClass*)v);
    }
    /* 超时/重试：真机的「非法地址组」回 8 字节非标准错误帧（框架解析不了），
     * 该帧滞留在接收缓冲 ⇒ 只能靠「超时后重试」把缓冲冲掉自愈。
     * 故：重试必须 ≥1；超时取小值以压住错误帧代价。 */
    XModbusClient_setTimeout((XModbusClient*)g_client, CLIENT_TIMEOUT_MS);
    XModbusClient_setNumberOfRetries((XModbusClient*)g_client, CLIENT_RETRIES);
    XModbusClient_setAutoReconnect((XModbusClient*)g_client, false);

    if (!XModbusDevice_connectDevice((XModbusDevice*)g_client)) {
        printf("  [FAIL] 串口 %s 打开失败（设备不存在 / 被其它程序占用 / 权限不足）\n",
               g_port);
        printf("         错误码=%d（ConnectionError=4）\n",
               (int)XModbusDevice_error((XModbusDevice*)g_client));
        fflush(stdout);
        XObject_deleteLater((XObject*)g_client);
        pump(50);
        return 3;
    }
    CHECK(XModbusDevice_state((XModbusDevice*)g_client) == XModbusDevice_ConnectedState,
          "串口打开并进入 Connected 状态");
    /* 开串口后再用 XSerialPort 接口复核一遍实际生效参数（文档给的用法）。 */
    {
        XSerialPort* port = XModbusRtuSerialClient_serialPort(g_client);
        if (port) {
            XSerialPort_setPortName(port, g_port);
            XSerialPort_setBaudRate(port, 115200, XSerialPort_AllDirections);
            XSerialPort_setDataBits(port, XSerialPort_Data8);
            XSerialPort_setParity(port, XSerialPort_NoParity);
            XSerialPort_setStopBits(port, XSerialPort_OneStop);
            printf("  [INFO] 串口实际参数：%s 波特=%d 数据位=%d 校验=%d 停止位=%d\n",
                   g_port,
                   (int)XSerialPort_baudRate(port, XSerialPort_AllDirections),
                   (int)XSerialPort_dataBits(port),
                   (int)XSerialPort_parity(port),
                   (int)XSerialPort_stopBits(port));
            fflush(stdout);
        } else {
            printf("  [WARN] 取不到 XSerialPort 对象（串口未创建）\n"); fflush(stdout);
        }
    }
    tlog("串口已连接，开始第 1 层：框架 RTU 栈直连真机");

    /* ==================== 第 1 层 ==================== */
    g_case = "L1-raw";
    {
        uint16_t v = 0xFFFF;
        printf("-- 第 1 层：框架 RTU 栈直连真机（绕开设备层）--\n"); fflush(stdout);

        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_RUN_STATE, &v, RAW_WAIT_MS)) {
            CHECKF(v == 3, "FC03 读 0x3000 运行状态 = %u（期望 3=停机）", (unsigned)v);
        } else {
            CHECK(false, "FC03 读 0x3000 运行状态（读失败）");
        }

        v = 0xFFFF;
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_FAULT, &v, RAW_WAIT_MS)) {
            CHECKF(v == 0x0000, "FC03 读 0x8000 故障码 = 0x%04X（期望 0x0000）", (unsigned)v);
        } else {
            CHECK(false, "FC03 读 0x8000 故障码（读失败）");
        }

        v = 0xFFFF;
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_MAX_FREQ, &v, RAW_WAIT_MS)) {
            CHECKF(v == 500, "FC03 读 0xF00A（P0.10 最大频率）= %u（期望 500=50.0Hz）",
                   (unsigned)v);
        } else {
            CHECK(false, "FC03 读 0xF00A 最大频率（读失败）");
        }

        if (rawWrite((XModbusClient*)g_client, g_unitId, M512_ADDR_FREQ_SET, 2000,
                     RAW_WAIT_MS)) {
            CHECK(true, "FC06 写 0x1000 = 2000（20% × 50Hz = 10.0Hz）应答正常");
        } else {
            CHECK(false, "FC06 写 0x1000 = 2000（写失败）");
        }
        v = 0xFFFF;
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_FREQ_SET, &v, RAW_WAIT_MS)) {
            CHECKF(v == 2000, "FC03 回读 0x1000 = %u（期望 2000）", (unsigned)v);
        } else {
            CHECK(false, "FC03 回读 0x1000（读失败）");
        }

        /* ---- 品牌层非标准错误帧：非法读必须**快速报错**且连接不死 ---- */
        /* 真机对「0x3000 单点地址连读 2 个」回本厂 8 字节非标准错误帧
         * （功能码不置 0x80）。通用 RTU 客户端按标准规则算帧长
         * （字节数字段 0x80 → 期望 133 字节）⇒ 帧永远留缓冲 ⇒ 请求超时；
         * 且缓冲只在重试时才清 ⇒ 重试次数为 0 时整条连接死掉。
         * 这里用**品牌层**客户端 XVfdM512RtuClient：它把本厂帧归一化成
         * 标准 5 字节异常帧再交框架 ⇒ 错误被正常识别，连接继续可用。 */
        {
            int err = -1;
            int64_t ms = 0;
            const bool settled = rawReadCount((XModbusClient*)g_client, g_unitId,
                                             M512_ADDR_RUN_STATE, 2,
                                             RAW_WAIT_MS, &err, &ms);
            CHECKF(settled, "非法读（0x3000 连读 2 个）已收口（%lld ms，未挂死）",
                   (long long)ms);
            CHECKF(err != (int)XModbusDevice_NoError,
                   "非法读被识别为错误（error=%d，非静默超时）", err);
            /* 连接存活：紧跟一笔合法读必须正常。 */
            v = 0xFFFF;
            if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_RUN_STATE, &v,
                        RAW_WAIT_MS)) {
                CHECKF(v == 3 || v == 1 || v == 2,
                       "错误帧后连接仍可用（0x3000 = %u）", (unsigned)v);
            } else {
                CHECK(false, "错误帧后连接仍可用（读失败）");
            }

        }
    }

    /* 第 1 层结束后断开串口：第 2 层由 Hub 的 connectAll 重新打开（避免双开）。 */
    XModbusDevice_disconnectDevice((XModbusDevice*)g_client);
    pump(100);
    tlog("第 1 层结束，串口已断开（第 2 层由 Hub 重新打开）");

    /* ==================== 第 2 层 ==================== */
    g_case = "L2-device";
    printf("-- 第 2 层：设备层闭环（XVfdM512 + Hub + 脚手架，单站）--\n"); fflush(stdout);

    CHECK(xvfdHarnessInit(&harness), "脚手架初始化（Hub + 映射 + 通道组）");
    g_hub = harness.m_hub;
    devId = xvfdHarnessAddStation(&harness, (XModbusClient*)g_client, g_unitId, "m512-real");
    CHECK(devId > 0, "真机从站注入 Hub（RTU 客户端 + 站号映射）");

    dev = XVfdM512_create();
    g_devObj = (XObject*)dev;
    CHECK(dev != NULL, "XVfdM512 设备创建");
    if (!dev) goto cleanup;

    CHECK(XVfdDevice_attach((XVfdDevice*)dev,
                             xvfdHarnessStationGroup(&harness, devId, g_unitId)),
          "设备挂接本站通道组");

    specs = XVfdM512_pointSpecs(&specCount);
    params = XVfdM512_paramDescs(&paramCount);
    bind = XVfdM512_modbusBinding();
    groups = bind->m_groups;
    groupCount = bind->m_groupCount;
    protocol = bind->m_protocol;
    CHECK(groupCount > 0 && protocol != NULL && specCount > 0,
          "品牌绑定可取（通信组 / 从站约束 / 点规格）");
    CHECK(xvfdHarnessMountBrand(&harness, (XVfdDevice*)dev, devId, g_unitId,
                                groups, groupCount, specs, specCount, protocol, bind),
          "按通信组装配品牌（组段 + 语义点 + 从站约束）");

    /* 参数通道：挂 P0.10（最大频率）参数点。**只读不写**——不改动现场参数。
     * P0.10 落在已建的 0xF008 组内 ⇒ 不新增读组（不增加总线占用）。 */
    {
        idMaxFreq = -1;
        for (i = 0; i < paramCount; ++i) {
            XVfdParamBinding pb;
            if (!XVfdBind_findParam(bind, (uint16_t)i, &pb)) continue;
            if (pb.m_addr == XVfdM512_AddrMaxFreq) { idMaxFreq = i; break; }
        }
        CHECK(idMaxFreq >= 0, "参数描述表含 P0.10 最大频率");
        if (idMaxFreq >= 0) {
            CHECK(xvfdHarnessMountParamBrand(&harness, (XVfdDevice*)dev, devId, g_unitId,
                                             groups, groupCount, &params[idMaxFreq],
                                             (uint16_t)idMaxFreq),
                  "装配 P0.10 参数点（AO 承载：读走 Hub 缓存）");
        }
    }

    /* 动作监督超时放宽：M512 通信组多（28 组），一轮读组轮转数百 ms，
     * 默认 2000 ms 会误报「写落定超时」。 */
    XVfdDevice_setActionTimeout((XVfdDevice*)dev, ACTION_TIMEOUT_MS);
    XVfdDevice_setStatusInterval((XVfdDevice*)dev, STATUS_INTERVAL_MS);
    /* 回归锁基线：挂载完成（状态定时器刚起）时的定时器 fd 计数。 */
    {
        int k, n = 0;
        for (k = 0; k < XFD_TABLE_SIZE; ++k) {
            XFileDescriptor* d = XFd_get((XFd)k);
            if (d && XFd_type((XFd)k) == XFD_TYPE_TIMER) ++n;
        }
        g_fdTimersAtMount = n;
        printf("  [INFO] 回归锁基线：挂载完成时定时器 fd = %d（状态间隔 %d ms）\n",
               g_fdTimersAtMount, STATUS_INTERVAL_MS);
    }
    printf("  [INFO] 动作监督超时放宽到 %d ms（迁就 M512 28 组轮转周期）\n",
           ACTION_TIMEOUT_MS);
    fflush(stdout);

    XObject_connect_2((XObject*)dev, XSignal(XVfdDevice_commandCompleted_signal),
                      onCommandCompleted);
    XObject_connect_2((XObject*)dev, XSignal(XVfdDevice_errorOccurred_signal),
                      onErrorOccurred);
    XObject_connect_2((XObject*)dev, XSignal(XVfdDevice_paramReadDone_signal),
                      onParamReadDone);

    g_hubStationLink = harness.m_stations[0].m_link;

    xvfdHarnessStart(&harness, 50);
    tlog("Hub 轮询启动（pollMs=50；M512 读组轮转一圈需数百 ms～秒级）");

    /* 等首个快照 */
    pump(4000);
    {
        const XVfdStatus* st = XVfdDevice_status_const((XVfdDevice*)dev);
        INFOF("设备层快照：valid=%d 运行态=%d(0未知/1停/2就绪/3正转/4反转) "
              "输出频率=%.1fHz 母线电压=%.1fV 故障码=%u",
              st ? (int)st->m_valid : -1,
              st ? (int)st->m_runState : -1,
              st ? st->m_outputFreqHz : -1.0,
              st ? st->m_busVoltageV : -1.0,
              st ? (unsigned)st->m_faultCode : 0u);
        CHECK(st && st->m_valid, "设备层出快照（Hub 读组轮转已回填）");
    }

    /* ---------- ② 命令源 / 频率源（0xF002 / 0xF003） ---------- */
    g_case = "L2-source";
    {
        bool ok = false;
        uint16_t v = 0xFFFF;
        CHECK(XVfdM512_setCommandSourceComm(dev), "发起切命令源（P0.02=2 → 0xF002）");
        if (waitActionDone(ACTION_TIMEOUT_MS, &ok)) {
            CHECKF(ok, "命令源切换动作落定 ok=%d（设备层写落定）", (int)ok);
        } else {
            CHECK(false, "命令源切换动作未在超时内落定（设备层未落定）");
        }
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_CMD_SOURCE, &v, RAW_WAIT_MS)) {
            CHECKF(v == 2, "回读 0xF002（P0.02 命令源）= %u（期望 2=通讯）", (unsigned)v);
        } else {
            CHECK(false, "回读 0xF002 命令源（读失败）");
        }

        ok = false;
        CHECK(XVfdM512_setFrequencySourceComm(dev), "发起切频率源（P0.03=9 → 0xF003）");
        if (waitActionDone(ACTION_TIMEOUT_MS, &ok)) {
            CHECKF(ok, "频率源切换动作落定 ok=%d（设备层写落定）", (int)ok);
        } else {
            CHECK(false, "频率源切换动作未在超时内落定（设备层未落定）");
        }
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_FREQ_SOURCE, &v, RAW_WAIT_MS)) {
            CHECKF(v == 9, "回读 0xF003（P0.03 频率源）= %u（期望 9=通讯给定）", (unsigned)v);
        } else {
            CHECK(false, "回读 0xF003 频率源（读失败）");
        }
    }

    /* ---------- ③ 遥测 ---------- */
    g_case = "L2-telemetry";
    {
        const XVfdStatus* st = XVfdDevice_status_const((XVfdDevice*)dev);
        uint16_t v = 0xFFFF;
        INFOF("遥测（设备层）：输出频率=%.1fHz 母线电压=%.1fV 输出电压=%.1fV "
              "输出电流=%.2fA 故障=%d 故障码=%u",
              st ? st->m_outputFreqHz : -1.0, st ? st->m_busVoltageV : -1.0,
              st ? st->m_outputVoltageV : -1.0, st ? st->m_outputCurrentA : -1.0,
              st ? (int)st->m_faulted : -1, st ? (unsigned)st->m_faultCode : 0u);
        CHECK(st && st->m_valid, "遥测快照有效");
        CHECK(st && !st->m_faulted && st->m_faultCode == 0, "无故障（故障码=0）");
        CHECK(st && st->m_runState == XVfdRunState_Stopped, "初始为停机态");
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_BUS_VOLTAGE, &v, RAW_WAIT_MS)) {
            INFOF("原始读 0x1002 母线电压 raw=%u", (unsigned)v);
        }
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_OUT_CURRENT, &v, RAW_WAIT_MS)) {
            INFOF("原始读 0x1004 输出电流 raw=%u", (unsigned)v);
        }
    }

    /* ---------- ③b 参数通道（读 P0.10） ---------- */
    g_case = "L2-param";
    {
        g_dev.paramReadCount = 0;
        g_dev.paramReadOk = false;
        g_dev.paramReadRaw = -1;
        CHECK(XVfdDevice_readParam((XVfdDevice*)dev, (uint16_t)idMaxFreq),
              "发起读参数 P0.10（最大频率）");
        {
            const int64_t deadline = XDateTime_currentMSecsSinceEpoch() + 8000;
            while (g_dev.paramReadCount == 0 &&
                   XDateTime_currentMSecsSinceEpoch() < deadline) {
                pump(20);
            }
        }
        CHECKF(g_dev.paramReadOk && g_dev.paramReadRaw == 500,
               "P0.10 参数回读 raw=%d（期望 500 = 50.0Hz，实机标定 0.1Hz 定点）",
               (int)g_dev.paramReadRaw);
    }

    /* ---------- ④ 调频 10Hz ---------- */
    g_case = "L2-setFreq";
    {
        bool ok = false;
        uint16_t v = 0xFFFF;
        CHECK(XVfdDevice_setFrequency((XVfdDevice*)dev, 10.0), "发起设定 10.0Hz");
        if (waitActionDone(ACTION_TIMEOUT_MS, &ok)) {
            CHECKF(ok, "调频动作落定 ok=%d（设备层写落定）", (int)ok);
        } else {
            CHECK(false, "调频动作未在超时内落定（设备层未落定）");
        }
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_FREQ_SET, &v, RAW_WAIT_MS)) {
            CHECKF(v == 2000, "回读 0x1000（通讯频率给定）= %u（期望 2000 = 10Hz/50Hz×10000）",
                   (unsigned)v);
        } else {
            CHECK(false, "回读 0x1000 频率给定（读失败）");
        }
        /* 停机态下运行频率应为 0（爬坡只在运行中发生）——打印实测值备查。 */
        v = 0xFFFF;
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_OUT_FREQ, &v, RAW_WAIT_MS)) {
            INFOF("停机态 0x1001 运行频率 raw=%u（停机时应为 0，爬坡在启动后发生）",
                  (unsigned)v);
        }
    }

    /* ---------- ⑤ 正转 ---------- */
    g_case = "L2-start";
    {
        bool ok = false;
        uint16_t state = 0xFFFF;
        uint16_t freq = 0xFFFF;
        CHECK(XVfdDevice_start((XVfdDevice*)dev, false), "发起正转运行（0x2000=1）");
        if (waitActionDone(ACTION_TIMEOUT_MS, &ok)) {
            CHECKF(ok, "启动动作落定 ok=%d（设备层写落定）", (int)ok);
        } else {
            CHECK(false, "启动动作未在超时内落定（设备层未落定）");
        }
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_RUN_STATE, &state, RAW_WAIT_MS)) {
            CHECKF(state == 1, "回读 0x3000 运行状态 = %u（期望 1=正转）", (unsigned)state);
        } else {
            CHECK(false, "回读 0x3000 运行状态（读失败）");
        }
        /* 加速时间 P0.17 = 100×0.1s = 10s ⇒ 3s 时还在爬坡（打印实测值） */
        pump(3000);
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_OUT_FREQ, &freq, RAW_WAIT_MS)) {
            INFOF("启动后 3s 的 0x1001 运行频率 raw=%u（加速时间 10s，此刻仍在爬坡）",
                  (unsigned)freq);
        }
        /* 等爬坡到稳态 */
        INFOF("等待 %d ms 让频率爬到稳态（加速时间 10s）", RAMP_WAIT_MS);
        pump(RAMP_WAIT_MS);
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_OUT_FREQ, &freq, RAW_WAIT_MS)) {
            CHECKF(freq >= 95 && freq <= 105,
                   "回读 0x1001 运行频率 = %u（期望 ≈100 = 10.0Hz，容差 ±5）", (unsigned)freq);
        } else {
            CHECK(false, "回读 0x1001 运行频率（读失败）");
        }
        {
            const XVfdStatus* st = XVfdDevice_status_const((XVfdDevice*)dev);
            INFOF("设备层快照：运行态=%d 输出频率=%.1fHz 设定频率=%.1fHz",
                  st ? (int)st->m_runState : -1,
                  st ? st->m_outputFreqHz : -1.0, st ? st->m_setFreqHz : -1.0);
            CHECK(st && st->m_runState == XVfdRunState_RunningForward,
                  "设备层运行态 = 正转（3000H=1）");
        }
    }

    /* ---------- ⑥ 反转 ---------- */
    g_case = "L2-reverse";
    {
        bool ok = false;
        uint16_t state = 0xFFFF;
        CHECK(XVfdDevice_start((XVfdDevice*)dev, true), "发起反转运行（0x2000=2）");
        if (waitActionDone(ACTION_TIMEOUT_MS, &ok)) {
            CHECKF(ok, "反转动作落定 ok=%d（设备层写落定）", (int)ok);
        } else {
            CHECK(false, "反转动作未在超时内落定（设备层未落定）");
        }
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_RUN_STATE, &state, RAW_WAIT_MS)) {
            CHECKF(state == 2, "回读 0x3000 运行状态 = %u（期望 2=反转）", (unsigned)state);
        } else {
            CHECK(false, "回读 0x3000 运行状态（读失败）");
        }
    }

    /* ---------- ⑦ 减速停机（0x2000=6，减速时间 10s） ---------- */
    g_case = "L2-stop";
    {
        bool ok = false;
        uint16_t state = 0xFFFF;
        CHECK(XVfdDevice_stop((XVfdDevice*)dev), "发起减速停机（0x2000=6）");
        if (waitActionDone(ACTION_TIMEOUT_MS, &ok)) {
            CHECKF(ok, "停机动作落定 ok=%d（设备层写落定）", (int)ok);
        } else {
            CHECK(false, "停机动作未在超时内落定（设备层未落定）");
        }
        INFOF("等待 %d ms 让减速完成（P0.18 = 100×0.1s = 10s）", RAMP_WAIT_MS);
        pump(RAMP_WAIT_MS);
        if (rawRead((XModbusClient*)g_client, g_unitId, M512_ADDR_RUN_STATE, &state, RAW_WAIT_MS)) {
            CHECKF(state == 3, "回读 0x3000 运行状态 = %u（期望 3=停机）", (unsigned)state);
        } else {
            /* 原始读失败：**不掩盖**——改用设备层 Hub 缓存（同一条链路的轮询值）
             * 作第二证据，并注明原始读失败的原因（框架缺陷）。 */
            uint16_t cached = 0xFFFF;
            if (hubReadRole(XVfdRole_StatusWord, &cached)) {
                CHECKF(cached == 3,
                       "原始读失败（框架 fd 表耗尽，见 [dbg]），但设备层 Hub 缓存里的 "
                       "0x3000 = %u（期望 3=停机）——实际值正确、仅传输路径失灵",
                       (unsigned)cached);
            } else {
                CHECK(false, "回读 0x3000 运行状态（原始读失败且设备层缓存无值）");
            }
        }
    }

    /* ---------- ⑧ Hub 轮询画像（诊断信息） ---------- */
    {
        const XVfdStatus* st = XVfdDevice_status_const((XVfdDevice*)dev);
        INFOF("收尾画像：设备层动作 %d 次（最后 op=%d ok=%d）错误 %d 次"
              "（最后错误：%s）",
              g_dev.doneCount, g_dev.lastOp, (int)g_dev.lastOk, g_dev.errorCount,
              g_dev.lastError[0] ? g_dev.lastError : "无");
        INFOF("Hub 轮询统计：pollOk=%u pollErr=%u 组数=%d 待写=%d",
              XModbusHub_pollOkCount(harness.m_hub, devId),
              XModbusHub_pollErrCount(harness.m_hub, devId),
              XModbusHub_readGroupCount(harness.m_hub, devId),
              XModbusHub_pendingWriteCount(harness.m_hub));
        (void)st;
    }

    /* ---------- ⑨ 框架缺陷取证：fd 表占用（定时器 fd 泄漏画像） ---------- */
    {
        int used = 0, timers = 0, nDev = 0, nOther = 0, k;
        for (k = 0; k < XFD_TABLE_SIZE; ++k) {
            XFileDescriptor* d = XFd_get((XFd)k);
            if (!d) continue;
            ++used;
            if (XFd_type((XFd)k) != XFD_TYPE_TIMER) continue;
            ++timers;
            if (d->object == (void*)g_devObj) ++nDev; else ++nOther;
        }
        printf("  [INFO] fd 表占用 %d/%d（定时器 %d：设备 %d / 其他 %d）\n",
               used, (int)XFD_TABLE_SIZE, timers, nDev, nOther);
        /* ---------- 回归锁：定时器 fd 不得随状态拍数线性增长 ---------- */
        /* 修复前：状态定时器每拍触发泄漏 1 个 fd（触发≠注销，fd 要 killTimer
         * 才释放），200ms 间隔约 25s 占满 128 槽 ⇒ RTU 客户端帧间定时器
         * 注册失败 ⇒ 完整应答帧滞留接收缓冲 ⇒ 全线超时。
         * 修复后（触发即注销）增量应接近 0。 */
        {
            int delta = timers - g_fdTimersAtMount;
            int limit = 8;   /* 允许少量抖动（重臂/超时/Hub 自身定时器），不容线性增长 */
            if (delta > limit) {
                printf("  [FAIL] 定时器 fd 增量 %d > %d（挂载时 %d → 现在 %d）"
                       "——状态定时器仍在泄漏\n",
                       delta, limit, g_fdTimersAtMount, timers);
                ++g_fail;
            } else {
                printf("  [OK] 定时器 fd 未线性增长（挂载时 %d → 现在 %d，Δ=%d <= %d）\n",
                       g_fdTimersAtMount, timers, delta, limit);
            }
        }
        fflush(stdout);
    }

cleanup:
    /* ---------- 收尾：拆设备层 → 安全停机 → 拆客户端 ---------- */
    if (dev) {
        XVfdDevice_detach((XVfdDevice*)dev);
        XClassDelete((XClass*)dev);
        dev = NULL;
    }
    if (harness.m_group || harness.m_hub) xvfdHarnessDestroy(&harness);
    pump(100);

    /* 安全停机（无条件执行：确保变频器一定停机） */
    safetyStop();

    if (g_client) {
        XModbusDevice_disconnectDevice((XModbusDevice*)g_client);
        XObject_deleteLater((XObject*)g_client);
        g_client = NULL;
    }
    pump(100);

    printf("=== 结束：失败用例 %d ===\n", g_fail);
    fflush(stdout);
    return g_fail;
}
