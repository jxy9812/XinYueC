/******************************************************************************
 * @file       xevent_leak_test.c
 * @brief      事件派发框架泄漏复查（桌面端，ESP8266 "WiFi 变哑" 任务前置步骤）。
 * @details    背景：esp8266 完整固件 ping 前 2 分钟 0% 丢包，随后
 *             varPoolUsed 以 ~150-200B/5s 单调上涨直至收包分配失败。
 *             交接文档（platforms/esp8266/docs/HANDOVER_2026-10-20.md §2.9/§3.1）怀疑
 *             XConsoleShell_notifyInput → postEvent → sendPostedEvents
 *             这条框架路径每次分配的事件对象未被逐个释放。
 *
 *             本测试在桌面端复刻 esp8266 app.c 主循环的全部事件用法，
 *             用 XMemory 统计（MULTIPOOL / SYSTEM / VARIABLEPOOL 三口径）
 *             做窗口化差分，判定该框架路径是否泄漏：
 *
 *             S1  shell 异步输入泵（复刻 app.c pump_entry）：
 *                 S1a 带 UART FIFO 门（当前板上代码，只有 FIFO 非空才
 *                     notifyInput + processEvents(0)）；
 *                 S1b 不带门（10-20 修复前的板上代码，每拍空转也
 *                     notifyInput + processEvents——即当时实测
 *                     "arena 5s 涨 768B" 的配置）；
 *             S2  纯框架路径：XEvent_create + postEvent 批投递 +
 *                 sendPostedEvents(NULL, 0) 派发（交接文档 §2.9
 *                 点名怀疑的函数）；
 *             S3  notifyInput 的确切队列路径：tryPostEvent（无锁队列）
 *                 + processEvents(0)（dispatcher 派发路径）；
 *             S4  定时器事件生命周期：1ms CoarseTimer + processEvents
 *                 泵（对应板上 shell 的 10ms 轮询定时器）。
 *
 *             判定：预热后按窗口差分，任一窗口池/堆用量持续线性增长
 *             （不收敛）即判泄漏，打印窗口表定位。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPrintf.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XEvent.h"
#include "XObject.h"
#include "XConsoleShell.h"
#include "XConsoleShellIo.h"
#include "XThread.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* ==================== 内存统计快照（三口径） ==================== */

typedef struct MemSnap {
    size_t pool;      /* MULTIPOOL 已用字节（XEvent_create 默认走此） */
    size_t poolTotal; /* MULTIPOOL 总容量（0 = 池未启用） */
    size_t sys;       /* SYSTEM 分配器在用字节 */
    size_t var;       /* VARIABLEPOOL arena 已用字节 */
} MemSnap;

static MemSnap mem_snap(void)
{
    MemSnap s;
    XMemoryStatistics a = XMemory_statistics_2(XMEMORY_TYPE_MULTIPOOL);
    XMemoryStatistics b = XMemory_statistics_2(XMEMORY_TYPE_SYSTEM);
    XMemoryStatistics c = XMemory_statistics_2(XMEMORY_TYPE_VARIABLEPOOL);
    s.pool = a.poolUsedBytes;
    s.poolTotal = a.poolTotalBytes;
    s.sys = b.systemBytes;
    s.var = c.poolUsedBytes;
    return s;
}

static void mem_print(const char* tag, const MemSnap* s)
{
    XPrintf("  %-10s pool=%7zu/%zu  sys=%7zu  var=%7zu\n",
            tag, s->pool, s->poolTotal, s->sys, s->var);
}

/* 窗口差分判定：预热后连续 WINDOWS 个窗口，任一窗口增量超过 eps 且不收敛
 * （最后一窗口仍有增长）即判泄漏。合法的一次性/有界分配会在早期窗口
 * 出现后归零（平台化），泄漏则每个窗口都线性增长。 */
#define WARMUP_ITERS   20000u
#define WINDOW_ITERS   20000u
#define WINDOW_COUNT   5u
#define EPS_POOL       256
#define EPS_SYS        1024
#define EPS_VAR        256

typedef struct WindowStat {
    long poolDelta;
    long sysDelta;
    long varDelta;
} WindowStat;

static bool window_verdict(const WindowStat* w, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; ++i) {
        if (w[i].poolDelta > EPS_POOL || w[i].poolDelta < -EPS_POOL) {
            /* 最后一窗口仍在动 = 不收敛 */
            if (count > 0 &&
                (w[count - 1].poolDelta > EPS_POOL ||
                 w[count - 1].poolDelta < -EPS_POOL))
                return false;
        }
        if (w[i].sysDelta > EPS_SYS || w[i].sysDelta < -EPS_SYS) {
            if (count > 0 &&
                (w[count - 1].sysDelta > EPS_SYS ||
                 w[count - 1].sysDelta < -EPS_SYS))
                return false;
        }
        if (w[i].varDelta > EPS_VAR || w[i].varDelta < -EPS_VAR) {
            if (count > 0 &&
                (w[count - 1].varDelta > EPS_VAR ||
                 w[count - 1].varDelta < -EPS_VAR))
                return false;
        }
    }
    return true;
}

static void window_report(const WindowStat* w, unsigned count)
{
    unsigned i;
    XPrintf("  %-8s %10s %10s %10s\n", "window", "dPool", "dSys", "dVar");
    for (i = 0; i < count; ++i) {
        XPrintf("  #%-7u %10ld %10ld %10ld\n", i + 1,
                w[i].poolDelta, w[i].sysDelta, w[i].varDelta);
    }
}

/* ==================== 自定义接收器（复刻 XCoreApplicationTest 模式） ==================== */

static unsigned long g_receivedEvents;

XCLASS_DEFINE_BEGING(LeakReceiver)
XCLASS_DEFINE_EXTEND_END(LeakReceiver, XObject);

typedef struct {
    XObject base;
    unsigned long received;
} LeakReceiver;

static bool VLeakReceiver_event(XObject* self, XEvent* e)
{
    (void)e;
    ++((LeakReceiver*)self)->received;
    ++g_receivedEvents;
    return XClass_Parent(XObject, EXObject_Event,
                         bool(*)(XObject*, XEvent*))(self, e);
}

static XVtable* LeakReceiver_class_init(void)
{
    XVTABLE_INIT_DEFAULT(LeakReceiver)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VLeakReceiver_event);
    return XVTABLE_DEFAULT;
}

static LeakReceiver* LeakReceiver_create(void)
{
    LeakReceiver* r = (LeakReceiver*)XMemory_malloc(sizeof(LeakReceiver),
                                                    XCLASS_DEFAULT_MEMORY_TYPE);
    if (!r) return NULL;
    XObject_init((XObject*)r);
    XClassGetVtable(r) = LeakReceiver_class_init();
    r->received = 0;
    Set_Class_Memory(r, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(r, true);
    return r;
}

/* ==================== 模拟 UART 传输（复刻 XConsoleShellTest transport） ==================== */

#define MOCK_IN_CAP  1024
#define MOCK_OUT_CAP 16384

typedef struct MockIo {
    char   in[MOCK_IN_CAP];
    size_t inLen;
    size_t inPos;
    char   out[MOCK_OUT_CAP];
    size_t outLen;
    unsigned long readCalls;
    unsigned long emptyReads;
    unsigned long writeBytes;
} MockIo;

static int64_t mock_read(void* userData, void* data, size_t size)
{
    MockIo* m = (MockIo*)userData;
    size_t avail, n;
    if (!m || (!data && size)) return -1;
    ++m->readCalls;
    avail = m->inLen - m->inPos;
    if (avail == 0) {
        ++m->emptyReads;
        return 0;
    }
    n = avail < size ? avail : size;
    if (n > 64) n = 64; /* 模拟 UART 单次突发上限 */
    memcpy(data, m->in + m->inPos, n);
    m->inPos += n;
    if (m->inPos == m->inLen) {
        m->inPos = 0;
        m->inLen = 0;
    }
    return (int64_t)n;
}

static int64_t mock_write(void* userData, const void* data, size_t size)
{
    MockIo* m = (MockIo*)userData;
    size_t n;
    if (!m) return -1;
    m->writeBytes += (unsigned long)size;
    n = size;
    if (n > MOCK_OUT_CAP - m->outLen - 1) {
        /* 环形丢弃旧输出，只防溢出，不影响泄漏判定 */
        m->outLen = 0;
    }
    if (n) {
        memcpy(m->out + m->outLen, data, n);
        m->outLen += n;
        m->out[m->outLen] = '\0';
    }
    return (int64_t)size;
}

static bool mock_flush(void* userData)
{
    (void)userData;
    return true;
}

static bool mock_attach(void* userData, XConsoleShell* shell)
{
    (void)userData;
    (void)shell;
    return true; /* 与板上一致：输入源就绪 → startAsync 起 10ms 轮询定时器 */
}

static void mock_detach(void* userData, XConsoleShell* shell)
{
    (void)userData;
    (void)shell;
}

static bool mock_echo(void* userData, bool enabled)
{
    (void)userData;
    (void)enabled;
    return true;
}

static bool mock_terminalSize(void* userData, int* columns, int* rows)
{
    (void)userData;
    if (!columns || !rows) return false;
    *columns = 80;
    *rows = 24;
    return true;
}

static void mock_stage(MockIo* m, const char* line)
{
    size_t n = strlen(line);
    if (n > MOCK_IN_CAP) n = MOCK_IN_CAP;
    memcpy(m->in, line, n);
    m->inLen = n;
    m->inPos = 0;
}

/* ==================== S1: shell 异步输入泵（复刻 app.c pump_entry） ==================== */

#if XCONSOLE_SHELL_ASYNC_ON
static bool scenario_shell_pump(bool fifoGate, const char* name)
{
    MockIo mock;
    XConsoleShellIo io;
    XConsoleShell* shell;
    WindowStat wins[WINDOW_COUNT];
    MemSnap base, prev, cur;
    unsigned long totalIters = WARMUP_ITERS + WINDOW_ITERS * WINDOW_COUNT;
    unsigned long i;
    unsigned w;

    memset(&mock, 0, sizeof(mock));
    memset(&io, 0, sizeof(io));
    io.read = mock_read;
    io.write = mock_write;
    io.flush = mock_flush;
    io.inputAttach = mock_attach;
    io.inputDetach = mock_detach;
    io.inputEcho = mock_echo;
    io.terminalSize = mock_terminalSize;
    io.userData = &mock;

    XPrintf("\n===== %s =====\n", name);
    shell = XConsoleShell_create(&io);
    if (!shell) {
        XPrintf("  [失败] XConsoleShell_create 返回 NULL\n");
        return false;
    }
    if (!XConsoleShell_startAsync(shell)) {
        XPrintf("  [失败] XConsoleShell_startAsync 返回 false\n");
        XClassDelete(shell);
        return false;
    }

    g_receivedEvents = 0;
    for (i = 1; i <= totalIters; ++i) {
        /* 每 500 拍喂一条空行（"\r\n"）：走完整 读入→回显→整行→提示符 链路，
         * 不引入命令表副作用，便于把增量归因到事件/行处理路径。 */
        if (i % 500u == 0u)
            mock_stage(&mock, "\r\n");

        if (fifoGate) {
            /* 当前板上代码：只有 UART RX FIFO 非空才驱动 shell */
            if (mock.inLen > mock.inPos) {
                (void)XConsoleShell_notifyInput(shell);
                XCoreApplication_processEvents(0);
            }
        } else {
            /* 修复前板上代码：每拍无条件 notifyInput + processEvents */
            (void)XConsoleShell_notifyInput(shell);
            XCoreApplication_processEvents(0);
        }

        if (i == WARMUP_ITERS) {
            base = mem_snap();
            prev = base;
        } else if (i > WARMUP_ITERS &&
                   ((i - WARMUP_ITERS) % WINDOW_ITERS) == 0u) {
            w = (unsigned)((i - WARMUP_ITERS) / WINDOW_ITERS - 1u);
            cur = mem_snap();
            wins[w].poolDelta = (long)cur.pool - (long)prev.pool;
            wins[w].sysDelta = (long)cur.sys - (long)prev.sys;
            wins[w].varDelta = (long)cur.var - (long)prev.var;
            prev = cur;
        }
    }

    (void)XConsoleShell_stopAsync(shell, 0);
    XClassDelete(shell);

    XPrintf("  迭代 %lu 拍，read 调用 %lu（空读 %lu），写出 %lu 字节\n",
            totalIters, mock.readCalls, mock.emptyReads, mock.writeBytes);
    mem_print("预热后", &base);
    window_report(wins, WINDOW_COUNT);
    {
        MemSnap endSnap = mem_snap();
        XPrintf("  总计增量: pool=%+ld sys=%+ld var=%+ld\n",
                (long)endSnap.pool - (long)base.pool,
                (long)endSnap.sys - (long)base.sys,
                (long)endSnap.var - (long)base.var);
    }
    if (window_verdict(wins, WINDOW_COUNT)) {
        XPrintf("  [通过] %s：事件/行处理路径内存收敛，无泄漏\n", name);
        return true;
    }
    XPrintf("  [失败] %s：检测到不收敛的内存增长（疑似泄漏）\n", name);
    return false;
}
#endif /* XCONSOLE_SHELL_ASYNC_ON */

/* ==================== S2: 纯框架 sendPostedEvents 路径 ==================== */

static bool scenario_send_posted_events(void)
{
    LeakReceiver* recv;
    WindowStat wins[WINDOW_COUNT];
    MemSnap base, prev, cur;
    unsigned long received = 0;
    unsigned long totalIters = WARMUP_ITERS + WINDOW_ITERS * WINDOW_COUNT;
    unsigned long i, created = 0;
    unsigned w;
    const XEventType kType = (XEventType)(XEVENT_TYPE_USER + 1);

    XPrintf("\n===== S2 纯框架路径: XEvent_create + postEvent + sendPostedEvents =====\n");
    recv = LeakReceiver_create();
    if (!recv) {
        XPrintf("  [失败] LeakReceiver_create 返回 NULL\n");
        return false;
    }

    for (i = 1; i <= totalIters; ++i) {
        XEvent* e = XEvent_create(kType);
        if (!e) {
            XPrintf("  [失败] XEvent_create 返回 NULL (iter=%lu)\n", i);
            XClassDelete((XObject*)recv);
            return false;
        }
        ++created;
        XCoreApplication_postEvent((XObject*)recv, e, XEVENT_PRIORITY_NORMAL);
        /* 每 8 个一批派发一次，模拟"攒一批再 sendPostedEvents"的用法 */
        if (i % 8u == 0u)
            XCoreApplication_sendPostedEvents(NULL, 0);

        if (i == WARMUP_ITERS) {
            base = mem_snap();
            prev = base;
        } else if (i > WARMUP_ITERS &&
                   ((i - WARMUP_ITERS) % WINDOW_ITERS) == 0u) {
            w = (unsigned)((i - WARMUP_ITERS) / WINDOW_ITERS - 1u);
            cur = mem_snap();
            wins[w].poolDelta = (long)cur.pool - (long)prev.pool;
            wins[w].sysDelta = (long)cur.sys - (long)prev.sys;
            wins[w].varDelta = (long)cur.var - (long)prev.var;
            prev = cur;
        }
    }
    XCoreApplication_sendPostedEvents(NULL, 0); /* 排空残余 */

    received = recv->received;
    XPrintf("  创建事件 %lu 个，接收 %lu 个（差 %ld）\n",
            created, received, (long)created - (long)received);
    XClassDelete((XObject*)recv);

    mem_print("预热后", &base);
    window_report(wins, WINDOW_COUNT);
    {
        MemSnap endSnap = mem_snap();
        XPrintf("  总计增量: pool=%+ld sys=%+ld var=%+ld\n",
                (long)endSnap.pool - (long)base.pool,
                (long)endSnap.sys - (long)base.sys,
                (long)endSnap.var - (long)base.var);
    }
    if (window_verdict(wins, WINDOW_COUNT) && received == created) {
        XPrintf("  [通过] S2：sendPostedEvents 逐个释放事件，创建/接收配对\n");
        return true;
    }
    if (received != created)
        XPrintf("  [失败] S2：事件创建/接收不配对（created=%lu received=%lu）\n",
                created, received);
    else
        XPrintf("  [失败] S2：检测到不收敛的内存增长（疑似泄漏）\n");
    return false;
}

/* ==================== S3: notifyInput 队列路径 tryPostEvent + processEvents ==================== */

static bool scenario_try_post_events(void)
{
    LeakReceiver* recv;
    WindowStat wins[WINDOW_COUNT];
    MemSnap base, prev, cur;
    unsigned long received = 0;
    unsigned long totalIters = WARMUP_ITERS + WINDOW_ITERS * WINDOW_COUNT;
    unsigned long i, created = 0;
    unsigned w;
    const XEventType kType = (XEventType)(XEVENT_TYPE_USER + 2);

    XPrintf("\n===== S3 队列路径: tryPostEvent + processEvents(0)（notifyInput 同款） =====\n");
    recv = LeakReceiver_create();
    if (!recv) {
        XPrintf("  [失败] LeakReceiver_create 返回 NULL\n");
        return false;
    }

    for (i = 1; i <= totalIters; ++i) {
        XEvent* e = XEvent_create(kType);
        if (!e) {
            XPrintf("  [失败] XEvent_create 返回 NULL (iter=%lu)\n", i);
            XClassDelete((XObject*)recv);
            return false;
        }
        ++created;
        if (!XCoreApplication_tryPostEvent((XObject*)recv, e,
                                           XEVENT_PRIORITY_NORMAL)) {
            XClassDelete(e); /* 入队失败由调用方释放，与 notifyInput 一致 */
        }
        XCoreApplication_processEvents(0); /* dispatcher 派发路径 */

        if (i == WARMUP_ITERS) {
            base = mem_snap();
            prev = base;
        } else if (i > WARMUP_ITERS &&
                   ((i - WARMUP_ITERS) % WINDOW_ITERS) == 0u) {
            w = (unsigned)((i - WARMUP_ITERS) / WINDOW_ITERS - 1u);
            cur = mem_snap();
            wins[w].poolDelta = (long)cur.pool - (long)prev.pool;
            wins[w].sysDelta = (long)cur.sys - (long)prev.sys;
            wins[w].varDelta = (long)cur.var - (long)prev.var;
            prev = cur;
        }
    }

    received = recv->received;
    XPrintf("  创建事件 %lu 个，接收 %lu 个（差 %ld）\n",
            created, received, (long)created - (long)received);
    XClassDelete((XObject*)recv);

    mem_print("预热后", &base);
    window_report(wins, WINDOW_COUNT);
    {
        MemSnap endSnap = mem_snap();
        XPrintf("  总计增量: pool=%+ld sys=%+ld var=%+ld\n",
                (long)endSnap.pool - (long)base.pool,
                (long)endSnap.sys - (long)base.sys,
                (long)endSnap.var - (long)base.var);
    }
    if (window_verdict(wins, WINDOW_COUNT) && received == created) {
        XPrintf("  [通过] S3：tryPostEvent/processEvents 路径内存收敛，配对正确\n");
        return true;
    }
    if (received != created)
        XPrintf("  [失败] S3：事件创建/接收不配对（created=%lu received=%lu）\n",
                created, received);
    else
        XPrintf("  [失败] S3：检测到不收敛的内存增长（疑似泄漏）\n");
    return false;
}

/* ==================== S4: 定时器事件生命周期 ==================== */

static bool scenario_timer_events(void)
{
    LeakReceiver* recv;
    WindowStat wins[WINDOW_COUNT];
    MemSnap base, prev, cur;
    const unsigned long totalIters = 2000u; /* 1ms/拍，共 ~2s 真实时间 */
    const unsigned long warmup = 200u;
    unsigned long i;
    unsigned w;
    XTimerId timerId;
    unsigned long fired = 0, lastFired = 0;

    XPrintf("\n===== S4 定时器事件: 1ms CoarseTimer + processEvents 泵 =====\n");
    recv = LeakReceiver_create();
    if (!recv) {
        XPrintf("  [失败] LeakReceiver_create 返回 NULL\n");
        return false;
    }
    timerId = XObject_startTimer_ms((XObject*)recv, 1, XTimerType_CoarseTimer);
    if (timerId == XTIMER_INVALID_ID) {
        XPrintf("  [失败] XObject_startTimer_ms 返回无效 ID\n");
        XClassDelete((XObject*)recv);
        return false;
    }

    for (i = 1; i <= totalIters; ++i) {
        XThread_usleep(1000);
        XCoreApplication_processEvents(0);
        fired = recv->received;

        if (i == warmup) {
            base = mem_snap();
            prev = base;
            lastFired = fired;
        } else if (i > warmup && ((i - warmup) % 400u) == 0u) {
            w = (unsigned)((i - warmup) / 400u - 1u);
            if (w >= WINDOW_COUNT) w = WINDOW_COUNT - 1u;
            cur = mem_snap();
            wins[w].poolDelta = (long)cur.pool - (long)prev.pool;
            wins[w].sysDelta = (long)cur.sys - (long)prev.sys;
            wins[w].varDelta = (long)cur.var - (long)prev.var;
            prev = cur;
        }
    }

    XObject_killTimer((XObject*)recv, timerId);
    fired = recv->received;
    XPrintf("  定时器事件 %lu 个（%lu 拍，窗口内 %lu 个）\n",
            fired, totalIters, fired - lastFired);
    XClassDelete((XObject*)recv);

    mem_print("预热后", &base);
    window_report(wins, WINDOW_COUNT);
    {
        MemSnap endSnap = mem_snap();
        XPrintf("  总计增量: pool=%+ld sys=%+ld var=%+ld\n",
                (long)endSnap.pool - (long)base.pool,
                (long)endSnap.sys - (long)base.sys,
                (long)endSnap.var - (long)base.var);
    }
    if (window_verdict(wins, WINDOW_COUNT) && fired > 0) {
        XPrintf("  [通过] S4：定时器事件创建/派发/释放配对，内存收敛\n");
        return true;
    }
    if (fired == 0)
        XPrintf("  [失败] S4：定时器一次都没响（桌面时钟驱动问题，无法判定泄漏）\n");
    else
        XPrintf("  [失败] S4：检测到不收敛的内存增长（疑似泄漏）\n");
    return false;
}

/* ==================== main ==================== */

int main(int argc, char* args[])
{
    char* argv[2];
    int failures = 0;
    int total = 0;
    XAbstractEventDispatcher* ed;

    argv[0] = (char*)"xevent_leak_test";
    argv[1] = NULL;
    (void)argc;
    (void)args;

    XPrintf("=== 事件派发框架泄漏复查（桌面端） ===\n");
    XPrintf("配置: MULTIPOOL_ON=%d VARIABLEPOOL_ON=%d HYBRID_THRESHOLD=%d "
            "STATISTICS_ON=%d\n",
            XMEMORY_MULTIPOOL_ON, XMEMORY_VARIABLEPOOL_ON,
            XMEMORY_HYBRID_THRESHOLD, XMEMORY_STATISTICS_ON);

    {
        XCoreApplication* app = XCoreApplication_create(1, argv);
        if (!app) {
            XPrintf("[失败] XCoreApplication_create 返回 NULL\n");
            return 2;
        }
    }
    ed = XCoreApplication_eventDispatcher();
    if (!ed) {
        XPrintf("[失败] 事件分发器未安装，processEvents 将是空转\n");
        return 2;
    }
    XPrintf("事件分发器: %p\n", (void*)ed);
    {
        MemSnap s = mem_snap();
        mem_print("初始", &s);
        if (s.poolTotal == 0)
            XPrintf("  [警告] MULTIPOOL 容量为 0，事件将走系统堆口径\n");
    }

#if XCONSOLE_SHELL_ASYNC_ON
    ++total; if (!scenario_shell_pump(true,
            "S1a shell 泵（带 UART FIFO 门，当前板上代码）")) ++failures;
    ++total; if (!scenario_shell_pump(false,
            "S1b shell 泵（无 FIFO 门，修复前板上代码）")) ++failures;
#else
    XPrintf("\n[跳过] S1：XCONSOLE_SHELL_ASYNC_ON=0（本构建裁剪了异步 shell）\n");
#endif

    ++total; if (!scenario_send_posted_events()) ++failures;
    ++total; if (!scenario_try_post_events()) ++failures;
    ++total; if (!scenario_timer_events()) ++failures;

    XPrintf("\n=== 汇总: %d/%d 通过 ===\n", total - failures, total);
    if (failures == 0) {
        XPrintf("结论: 事件派发框架（notifyInput/postEvent/tryPostEvent/"
                "sendPostedEvents/processEvents/定时器事件）在桌面端"
                "未发现泄漏。\n");
        XPrintf("若 esp8266 上 varPoolUsed 仍单调上涨，泄漏源应锁定在 "
                "esp2glue 收包 pbuf 路径（见交接文档 §3.1 建议 2："
                "esp2glue_alloc_for_recv 环形记录表核对释放）。\n");
        return 0;
    }
    XPrintf("结论: 事件派发框架存在泄漏，按上方窗口表定位增量来源。\n");
    return 1;
}
