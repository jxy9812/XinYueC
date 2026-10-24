/**
 * @file x_timer_coarse_repro.c
 * @brief CoarseTimer（时间轮）路径的最小复现程序（2026-10-22）
 *
 * @details 复现 ESP8266 NONOS3 固件上的崩溃：
 *
 *   #1  建 XCoreApplication（建事件分发器）
 *   #2  注册一枚 CoarseTimer（10ms）——与 XConsoleShell_startAsync 注册的
 *       串口轮询定时器同型
 *   #3  反复 XCoreApplication_processEvents(0) 驱动时间轮 → 定时器到期
 *
 * ESP8266 上的现象（cap_tbf.log）：
 *   Fatal exception 0(IllegalInstructionCause)
 *   epc1=0x40201013（XAtomic_exchange_bool + 3，跳进函数中段）
 *   excvaddr=0x40266404（指向 rodata 数据符号 = 野指针 / 坏 vtable）
 *
 * 桌面端（Windows，XPLATFORM_WINDOWS=1）跑同一个场景：
 *   · 崩 → 纯框架 bug，用 gdb 直接定位；
 *   · 不崩 → 与平台/位宽相关（32 位裸机路径），需对比 XThreadData_current()
 *     等裸机分支。
 *
 * 用法：
 *   x_timer_coarse_repro [轮数] [定时器间隔ms]
 * 默认 2000 轮 × 10ms。退出码 0 = 正常；非 0 = 崩了。
 */
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
static void repro_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void repro_sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
#endif

#include "CXinYueConfig.h"
#include "XCoreApplication.h"
#include "XObject.h"
#include "XTimer.h"
#include "XDateTime.h"

static int s_fired = 0;

static void on_timeout(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++s_fired;
}

int main(int argc, char** argv)
{
    int rounds = (argc > 1) ? atoi(argv[1]) : 200;
    int intervalMs = (argc > 2) ? atoi(argv[2]) : 10;
    int sleepMs = (argc > 3) ? atoi(argv[3]) : 5;
    char* argv0[] = { (char*)"coarse_repro", 0 };
    XCoreApplication* app;
    XObject* obj;
    XTimer* timer;
    int i;

    printf("[repro] rounds=%d interval=%dms sleep=%dms\n",
           rounds, intervalMs, sleepMs);
    fflush(stdout);

    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 1, argv0);
    if (!app) {
        printf("[repro] XCoreApplication_create FAILED\n");
        return 2;
    }
    printf("[repro] dispatcher=%s\n",
           XCoreApplication_eventDispatcher() ? "ok" : "NULL");
    fflush(stdout);

    obj = XObject_create();
    timer = XTimer_create();
    XTimer_setInterval(timer, intervalMs);
    XTimer_setSingleShot(timer, false);
    XTimer_setAutoDelete(timer, false);
    XTimer_setTimerType(timer, XTimerType_CoarseTimer);
    XObject_connect_2(timer, XSignal(XTimer_timeout_signal), on_timeout);
    XTimer_start_base(timer);
    printf("[repro] CoarseTimer armed: timer=%p\n", (void*)timer);
    fflush(stdout);

    for (i = 0; i < rounds; ++i) {
        /* 与 ESP8266 固件同型：非阻塞，每拍一次。
         * 每拍之间留出真实时间流逝，否则 10ms 的 CoarseTimer 一次都
         * 不会到期，探针就测不到时间轮的到期消费路径。 */
        repro_sleep_ms(sleepMs);
        XCoreApplication_processEvents(0);
        if ((i % 20) == 0) {
            printf("[repro] i=%d fired=%d now=%lld\n",
                   i, s_fired, (long long)XDateTime_currentMSecsSinceEpoch());
            fflush(stdout);
        }
    }

    printf("[repro] DONE fired=%d\n", s_fired);
    return (s_fired > 0) ? 0 : 3;
}
