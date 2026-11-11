/**
 * @file       xq_align_run.c
 * @brief      Qt 6.8 对齐测试运行器（无 main，供 Test 菜单 / `--test qt-align` 调用）。
 * @details    遍历 `xq_align.h` 自注册的用例并逐个执行：
 *             - `--list`            只列用例名；
 *             - `--filter=<子串>`   只跑名称含该子串的用例；
 *             - `--shard=N/M`       只跑排序后序号 i%M==N 的子集（ASan 分片）；
 *             - `--log=<文件>`      stdout/stderr 落盘（ASan abort 时保留报告）；
 *             - `--leak-tolerance=N` 每用例堆增长容忍字节数（默认 0）；
 *             - `--help`            帮助。
 *
 *             泄漏判据（对齐 AGENTS.md「泄漏结论必须用堆计数下」）：
 *             用例前后各取一次框架分配器计数（系统堆 + 多级池 + 可变池
 *             在用字节）；增长超容忍即**再跑一遍同一用例**——第二遍不再
 *             增长说明首遍是一次性懒初始化（只提示 `[INIT]`），第二遍仍
 *             增长才判 `[LEAK]` 失败。断言失败不参与重跑判定。
 *
 * @note       无头可跑，不依赖平台窗口/显示服务；堆计数不可用时自动
 *             跳过泄漏门并打印提示（断言仍生效）。
 * @note       本文件**不再带 main**：正式入口是 Test 菜单「Qt6.8 对齐」项
 *             与 `bin/XinYueC_Static --test qt-align`，避免 TEST_FILE 多 main。
 * @author     XinYueC 团队
 */
#include "xq_align.h"
#include "xq_align_run.h"

#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XMemory.h"
#include "XObject.h"
#include "XWidget.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ==================== 断言/登记基础设施 ==================== */

int xq_checks = 0;
int xq_failures = 0;

/** @brief 泄漏复核重跑期间抑制重复的失败打印。 */
static int s_quiet = 0;

static XQTestCase* s_head = NULL;

void xq_register_case(XQTestCase* tc)
{
    if (!tc) return;
    tc->next = s_head;
    s_head = tc;
}

void xq_check_impl(bool cond, const char* expr, const char* file, int line,
                   const char* fmt, ...)
{
    ++xq_checks;
    if (cond) return;
    ++xq_failures;
    if (s_quiet) return;
    fprintf(stderr, "[FAIL] %s:%d  %s", file ? file : "?", line,
            expr ? expr : "");
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        fprintf(stderr, "  (");
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, ")");
        va_end(ap);
    }
    fprintf(stderr, "\n");
}

void xq_fail_at(const char* file, int line, const char* fmt, ...)
{
    ++xq_failures;
    if (s_quiet) return;
    fprintf(stderr, "[FAIL] %s:%d  ", file ? file : "?", line);
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
    fprintf(stderr, "\n");
}

void xq_fail_simple(const char* expr, const char* file, int line)
{
    xq_check_impl(false, expr, file, line, NULL);
}

/* ==================== 堆计数 ==================== */

/** @brief 当前框架分配器在用字节合计（系统堆 + 多级池 + 可变池）。 */
static size_t xq_mem_now(void)
{
    XMemoryStatistics sys = XMemory_statistics_2(XMEMORY_TYPE_SYSTEM);
    XMemoryStatistics mp = XMemory_statistics_2(XMEMORY_TYPE_MULTIPOOL);
    XMemoryStatistics vp = XMemory_statistics_2(XMEMORY_TYPE_VARIABLEPOOL);
    return sys.systemBytes + mp.poolUsedBytes + vp.poolUsedBytes;
}

/**
 * @brief 预热：把对象基础设施的懒初始化（线程数据/事件分发器等）提前建好。
 * @details 这些全局单例一旦建立即常驻，若落在第一个用例里会被误判为泄漏。
 */
static void xq_warmup(void)
{
    XObject* obj = XObject_create();
    if (obj) XClassDelete((XClass*)obj);
    {
        XWidget* w = XWidget_create(NULL, 0);
        if (w) XClassDelete((XClass*)w);
    }
}

/* ==================== 用例执行 ==================== */

typedef struct XQRunResult
{
    int checks;
    int failures;
    long long leakDelta;   /**< 首遍堆增长；负数为净释放。 */
    long long secondDelta; /**< 复核遍堆增长；未复核时为 0。 */
    int reran;             /**< 是否因疑似泄漏复核跑了两遍。 */
    double elapsedMs;
} XQRunResult;

/** @brief 单遍执行（重置断言计数，返回失败数）。 */
static int xq_run_once(const XQTestCase* tc)
{
    xq_checks = 0;
    xq_failures = 0;
    tc->fn();
    /* 回收拍：时间轮的 killTimer 是「标记删除 + 下一拍扫描释放」的延迟回收
     * （头注释见 XTimeWheelGroup.c:62-135：槽内链表由生产者无锁插入，消费者
     * 下一轮原子摘下后 delete_timer_node ⇒ 不能就地释放）。用例自己不跑事件
     * 循环时（如 XFocusFrame 的构造/贴合用例），被取消的定时器节点要留到
     * 下一次轮询才回收，泄漏门会把「待回收」误判成「泄漏」（实测四个
     * XFocusFrame 用例各 +120~904 字节，且复核遍同步增长）。
     * 真实应用的事件循环每秒都在跑，回收拍是宿主必须补的那一拍；它只释放
     * 已标记删除的节点，不会掩盖真正的泄漏（真泄漏在第二遍仍增长）。
     * Qt 6.8 对照：QTimerInfoList::unregisterTimer 立即删除定时器信息，
     * 没有这个延迟——差别在回收时机，不在对外语义。 */
    (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
    /* DeferredDelete 拍：deleteLater 的投递判据是
     * `eventLevel > currentLevel`（XDeferredDeleteEvent_shouldDeliver），
     * 用例主体在事件循环外调用 deleteLater 时 loopLevel/scopeLevel 均为 0，
     * 普通 processEvents 永不投递该事件 ⇒ 对象与事件永久滞留，泄漏门把
     * 「已排队的延迟回收」误判成「泄漏」（实测 XActionGroup/XButtonGroup/
     * XDialogButtonBox/XToolBar 各 +0.6~3.5KB 且复核遍同步增长，而同一
     * 场景在真实事件循环里第二遍即归零）。显式点名 DeferredDelete 的
     * sendPostedEvents 正是 Qt 宿主在 exec 退出时补的那一拍
     * （QCoreApplication::exec → sendPostedEvents(nullptr,
     * QEvent::DeferredDelete)，本框架 XCoreApplication_exec 同款），
     * 只兑现已排队的销毁，不会掩盖真泄漏（真泄漏没有待投递事件）。 */
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    /* 析构期间可能再投递子对象移除/布局失效等事件，补一拍普通派发。 */
    (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
    return xq_failures;
}

static XQRunResult xq_run_case(const XQTestCase* tc, long long tolerance)
{
    XQRunResult r;
    size_t before;
    size_t after;
    clock_t t0;
    int firstFailures;

    memset(&r, 0, sizeof(r));
    before = xq_mem_now();
    t0 = clock();
    firstFailures = xq_run_once(tc);
    r.elapsedMs = (double)(clock() - t0) * 1000.0 / (double)CLOCKS_PER_SEC;
    after = xq_mem_now();
    r.checks = xq_checks;
    r.failures = firstFailures;
    r.leakDelta = (long long)after - (long long)before;

    if (r.leakDelta > tolerance) {
        size_t b2 = xq_mem_now();
        s_quiet = 1;
        (void)xq_run_once(tc);
        s_quiet = 0;
        r.secondDelta = (long long)xq_mem_now() - (long long)b2;
        r.reran = 1;
    }
    return r;
}

/* ==================== 主程序 ==================== */

#define XQ_MAX_CASES 4096

/** @brief 从注册链表收集并按键名排序（稳定输出顺序）。 */
static int xq_collect(XQTestCase** out)
{
    int n = 0;
    XQTestCase* it = s_head;
    while (it && n < XQ_MAX_CASES) {
        int i = n;
        while (i > 0 && strcmp(out[i - 1]->name, it->name) > 0) {
            out[i] = out[i - 1];
            --i;
        }
        out[i] = it;
        ++n;
        it = it->next;
    }
    if (it) {
        fprintf(stderr, "[WARN] 用例数超过 %d，其余被忽略\n", XQ_MAX_CASES);
    }
    return n;
}

int xq_align_run(int argc, char** argv)
{
    static XQTestCase* cases[XQ_MAX_CASES];
    const char* filter = NULL;
    const char* logPath = NULL;
    long long tolerance = 0;
    int listOnly = 0;
    int shardIndex = 0;
    int shardCount = 1;
    int total = 0;
    int ran = 0;
    int failedCases = 0;
    int leakCases = 0;
    int checks = 0;
    int failures = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (strcmp(a, "--list") == 0) {
            listOnly = 1;
        } else if (strcmp(a, "--all") == 0) {
            /* 缺省行为即全部执行；显式开关用于脚本可读性。 */
        } else if (strncmp(a, "--filter=", 9) == 0) {
            filter = a + 9;
        } else if (strncmp(a, "--shard=", 8) == 0) {
            /* 分片：按排序后的用例序号 i 取 i % M == N 的子集，供 ASan
             * 多进程并行定位（每个进程各自在第一个内存错误处 abort）。 */
            int n = 0;
            int m = 0;
            if (sscanf(a + 8, "%d/%d", &n, &m) != 2 || m < 1 || n < 0 || n >= m) {
                fprintf(stderr, "无效 --shard：%s（格式 N/M，0<=N<M）\n", a);
                return 2;
            }
            shardIndex = n;
            shardCount = m;
        } else if (strncmp(a, "--log=", 6) == 0) {
            logPath = a + 6;
        } else if (strncmp(a, "--leak-tolerance=", 17) == 0) {
            tolerance = strtoll(a + 17, NULL, 10);
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            fprintf(stdout,
                    "用法: XinYueC_Static --test qt-align "
                    "[--list] [--filter=<子串>] [--shard=N/M] "
                    "[--log=<文件>] [--leak-tolerance=N]\n");
            return 0;
        } else if (a[0] != '\0') {
            fprintf(stderr, "未知参数：%s（--help 查看用法）\n", a);
            return 2;
        }
    }

    total = xq_collect(cases);

    if (listOnly) {
        for (i = 0; i < total; ++i) {
            fprintf(stdout, "%s\n", cases[i]->name);
        }
        return 0;
    }

    /* --log：把 stdout/stderr 同时落到文件。ASan abort 时进程被信号终止，
     * 调用方拿不到正常退出输出；文件里仍有完整 ASan 报告与用例进度。
     * stderr 用 "w" 先清空、stdout 用 "a" 追加（不截断已写的 ASan 段）。 */
    if (logPath && logPath[0] != '\0') {
        if (!freopen(logPath, "w", stderr)) {
            fprintf(stderr, "[WARN] 无法打开日志文件：%s\n", logPath);
        }
        (void)freopen(logPath, "a", stdout);
    }

    /* 宿主应用：Qt 的测试宿主（QTEST_MAIN）在 QCoreApplication/QApplication
     * 之下运行，任何用到事件循环/定时器的用例都以它为前提；本框架同口径
     * ——`XCoreApplication` 构造时经 `XThread_createMainThread` 建立主线程
     * 数据（Qt 6.8 `QCoreApplicationPrivate::theMainThread`），没有它
     * `XThreadData_mainThread()` 为 NULL，`XTimeWheelGroup_global_init`
     * （XTimeWheelGroup.c:882）解引用即段错误，`XEventLoop_create` 一调就崩。
     * 缺失应用此前让 `XObject.deleteLaterInLoop` 等用例直接 SIGSEGV 中断整轮
     * 回归（后面的用例根本没跑）。与仓库其它测试宿主一致：先取是否已存在，
     * 只有本处创建才负责释放，避免误删外层应用。 */
    {
        bool owned = (XCoreApplication_instance() == NULL);
        XCoreApplication* app = XCoreApplication_create(0, NULL);
        if (!app) {
            fprintf(stderr, "[FAIL] XCoreApplication 创建失败："
                            "事件循环/定时器用例无法执行\n");
            return 3;
        }

        XMemory_setStatisticsEnabled(true);
        if (!XMemory_statisticsEnabled()) {
            fprintf(stderr,
                    "[WARN] 框架堆统计不可用（XMEMORY_STATISTICS_ON=0）："
                    "本次跳过泄漏门，仅执行断言。\n");
            tolerance = (long long)(~(unsigned long long)0 >> 1);
        }

        xq_warmup();

        fprintf(stdout, "Qt6.8 对齐测试：%d 个用例（过滤：%s）\n", total,
                filter ? filter : "无");

        for (i = 0; i < total; ++i) {
            const XQTestCase* tc = cases[i];
            XQRunResult r;
            int leak = 0;

            if (filter && strstr(tc->name, filter) == NULL) continue;
            if (shardCount > 1 && (i % shardCount) != shardIndex) continue;

            ++ran;
            fprintf(stdout, "[RUN ] %s\n", tc->name);
            fflush(stdout);
            r = xq_run_case(tc, tolerance);

            checks += r.checks;
            failures += r.failures;
            if (r.failures > 0) {
                ++failedCases;
            }

            if (r.leakDelta > tolerance) {
                if (r.reran && r.secondDelta <= tolerance) {
                    fprintf(stdout, "[INIT] %s：首遍 +%lld 字节，复核遍 %+lld "
                                    "（判为懒初始化，不算泄漏）\n",
                            tc->name, r.leakDelta, r.secondDelta);
                } else {
                    leak = 1;
                    ++leakCases;
                    fprintf(stderr,
                            "[LEAK] %s：+%lld 字节%s（容忍 %lld）\n",
                            tc->name, r.leakDelta,
                            r.reran ? "，且复核遍仍增长" : "", tolerance);
                }
            }

            if (r.failures == 0 && !leak) {
                fprintf(stdout, "[ OK ] %s（%d 项断言，%.1f ms）\n", tc->name,
                        r.checks, r.elapsedMs);
            } else {
                fprintf(stdout, "[FAIL] %s（%d 项断言，%d 失败%s）\n", tc->name,
                        r.checks, r.failures, leak ? "，泄漏" : "");
            }
            fflush(stdout);
        }

        fprintf(stdout,
                "\nQt6.8 对齐测试汇总：用例 %d/%d 执行，断言 %d，失败 %d，"
                "失败用例 %d，泄漏用例 %d\n",
                ran, total, checks, failures, failedCases, leakCases);

        if (owned) XClassDelete((XClass*)app);
    }

    return (failedCases == 0 && leakCases == 0) ? 0 : 1;
}
