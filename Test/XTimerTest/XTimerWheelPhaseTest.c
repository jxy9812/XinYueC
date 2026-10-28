/**
 * @file XTimerWheelPhaseTest.c
 * @brief 时间轮高层槽位「相位落槽」回归测试（2026-10-28）
 *
 * @details 回归缺陷：addTimer_lockfree 旧实现按 offset = timeout/本级槽跨度
 *          把定时器放进 (m_tick+offset) 槽，而 (m_tick) 槽才是**下一个边界**
 *          要级联的槽——周期中段注册的高层轮定时器要多等一整圈：全局轮
 *          L2 槽距 900ms，1000ms CoarseTimer 实测固定 1800ms 触发、每次晚
 *          ~800ms（XGuiDemo 顶栏系统时钟「1s 刷新变 1.8s」实证，2026-10-28）。
 *
 *          本测试用**错相注册**覆盖该缺陷面：同一 1000ms 间隔按 0/170/340ms
 *          错相启动三枚（覆盖不同边界相位）+ 2500ms 一枚（跨多边界的残余
 *          降级路径）+ 一枚走分发器 XTimer 路径的对照（XObject_startTimer_ms
 *          → XDeviceTimer_schedule → 全局轮，与应用层定时器同链路），逐次
 *          断言触发间隔。缺陷复现时 1000ms 组间隔落在 1800ms、2500ms 组落
 *          在 3600ms，均远超容差必红。
 *
 *          容差 -120/+150ms：CoarseTimer 触发经事件循环消费，量化 ≤ 一圈
 *          调度节奏（实测 ~20ms），留足余量；旧缺陷的 900ms 整圈量化落在
 *          容差外。
 *
 * @note 时间只取 XDateTime_currentMSecsSinceEpoch（框架时间源），等待只用
 *       XThread_msleep（跨平台），不引任何平台头文件——ESP8266/Win32 等
 *       各平台可编译运行。
 *
 * 用法：XTimerWheelPhaseTest [运行毫秒]（缺省 8200）。退出码 = 失败断言数。
 */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include "CXinYueConfig.h"
#include "XCoreApplication.h"
#include "XObject.h"
#include "XTimer.h"
#include "XTimeWheelGroup.h"
#include "XDateTime.h"
#include "XThread.h"
#include "XPrintf.h"

/* 每枚定时器的触发记录（8.2s 运行窗内 1000ms 至多 ~8 次，64 槽足够） */
#define PHASE_FIRE_CAP 64
#define PHASE_TIMER_COUNT 5

typedef struct PhaseFireLog
{
    const char* m_name;         /**< 定时器名（报告用）。 */
    size_t m_intervalMs;        /**< 标称间隔（ms）。 */
    int64_t m_fires[PHASE_FIRE_CAP]; /**< 每次触发的框架时刻（ms）。 */
    int m_count;                /**< 实际触发次数。 */
} PhaseFireLog;

static PhaseFireLog s_logs[PHASE_TIMER_COUNT] = {
    { "wheel-1000ms-phase0", 1000, { 0 }, 0 },
    { "wheel-1000ms-phase170", 1000, { 0 }, 0 },
    { "wheel-1000ms-phase340", 1000, { 0 }, 0 },
    { "wheel-2500ms", 2500, { 0 }, 0 },
    { "dispatcher-XTimer-1000ms", 1000, { 0 }, 0 }
};

/** @brief 轮定时器回调：只记时刻（框架时间源）。 */
static void phase_wheel_callback(void* userData, XTimerData* timer)
{
    PhaseFireLog* log = (PhaseFireLog*)userData;
    (void)timer;
    if (log && log->m_count < PHASE_FIRE_CAP)
        log->m_fires[log->m_count++] = XDateTime_currentMSecsSinceEpoch();
}

/** @brief 向全局时间轮注册一枚周期 Coarse 定时器。 */
static void phase_register_wheel_timer(PhaseFireLog* log)
{
    XTimeWheelGroup* wheel = XTimeWheelGroup_global();
    XTimerData data = { 0 };
    XTimerData_setInterval(&data, log->m_intervalMs);
    XTimerData_setTimerCallback(&data, phase_wheel_callback);
    XTimerData_setUserData(&data, log);
    XTimeWheelGroup_addTimerMs_base(wheel, data);
}

int main(int argc, char** argv)
{
    int failures = 0;
    int64_t runMs = (argc > 1) ? (int64_t)atoll(argv[1]) : 8200;
    int64_t deadline;
    XCoreApplication* app;
    int i;

    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        fprintf(stderr, "[phase-test] XCoreApplication 创建失败\n");
        return 1;
    }

    /* ---- 错相注册：0/170/340ms 三相同 1000ms + 2500ms 跨边界 ----
     * 170/340 刻意取 L2 槽距 900ms 的非整分段：旧落槽下不同相位的亏欠量
     * 不同（最坏整圈 900ms），错相保证任一相位亏欠都被至少一枚覆盖。 */
    phase_register_wheel_timer(&s_logs[0]);
    XThread_msleep(170);
    phase_register_wheel_timer(&s_logs[1]);
    XThread_msleep(170);
    phase_register_wheel_timer(&s_logs[2]);
    XThread_msleep(170);
    phase_register_wheel_timer(&s_logs[3]);

    /* 分发器路径对照（与应用层 XObject_startTimer_ms 同链路）。 */
    {
        XTimer* dispatcherTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (dispatcherTimer) {
            XTimer_setInterval(dispatcherTimer,
                               s_logs[4].m_intervalMs);
            XTimer_setSingleShot(dispatcherTimer, false);
            XTimer_setAutoDelete(dispatcherTimer, false);
            XTimer_setTimerType(dispatcherTimer, XTimerType_CoarseTimer);
            XTimer_setTimerCallback(dispatcherTimer, phase_wheel_callback);
            XTimer_setUserData(dispatcherTimer, &s_logs[4]);
            XTimer_start_base(dispatcherTimer);
        }
    }

    /* ---- 驱动事件循环至运行窗结束（非阻塞拍 + 跨平台小睡） ---- */
    deadline = XDateTime_currentMSecsSinceEpoch() + runMs;
    while (XDateTime_currentMSecsSinceEpoch() < deadline) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
    }

    /* ---- 逐枚断言：间隔数 ≥ 下限 且 每个间隔落在容差内 ---- */
    XPrintf("========== 时间轮相位落槽回归测试 ==========\n");
    for (i = 0; i < PHASE_TIMER_COUNT; ++i)
    {
        PhaseFireLog* log = &s_logs[i];
        int64_t minDelta = 0, maxDelta = 0;
        int deltaCount = log->m_count - 1;
        int j;
        bool intervalOk = true;
        bool countOk;
        int64_t minFires = (log->m_intervalMs >= 2500) ? 2 : 5;

        for (j = 0; j < deltaCount; ++j)
        {
            int64_t delta = log->m_fires[j + 1] - log->m_fires[j];
            if (j == 0 || delta < minDelta) minDelta = delta;
            if (j == 0 || delta > maxDelta) maxDelta = delta;
            /* 容差：-120/+150ms（消费量化 ≤ ~20ms，留足余量）。 */
            if (delta < (int64_t)log->m_intervalMs - 120 ||
                delta > (int64_t)log->m_intervalMs + 150)
                intervalOk = false;
        }
        countOk = (deltaCount >= minFires);

        XPrintf("[%s] %s: 触发 %d 次, 间隔 %lld~%lld ms (标称 %llu ms)\n",
                (intervalOk && countOk) ? "通过" : "失败",
                log->m_name, log->m_count,
                (long long)minDelta, (long long)maxDelta,
                (unsigned long long)log->m_intervalMs);
        if (!intervalOk) {
            XPrintf("       -> 存在超出容差的间隔（疑似相位落槽缺陷）\n");
            ++failures;
        }
        if (!countOk) {
            XPrintf("       -> 触发次数不足（间隔下限 %lld，运行窗 %lld ms）\n",
                    (long long)minFires, (long long)runMs);
            ++failures;
        }
    }

    XPrintf("========== %s（失败断言 %d）==========\n",
            failures == 0 ? "全部通过" : "存在失败", failures);
    fflush(stdout);
    return failures;
}
