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
 *          2026-10-29 增补「级联重落槽」确定性段（假时钟）：覆盖
 *          cascade_timers→addTimer_lockfree 重分配路径——旧相位测试只覆盖
 *          添加时落槽，板上 50ms 轮询定时器 921ms 冻结（级联时刻 remaining
 *          恰等于低级轮全跨度）从该路径漏过。
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
#include <string.h>

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

/* ==================== 级联重落槽回归（确定性假时钟 2026-10-29） ====================
 * 回归缺陷：cascade_timers 对放不进低级轮的节点走 addTimer_lockfree 重分配，
 * 相位修正在级联上下文算出 offset=0 时把节点放回「本次级联刚弹空、返回后
 * 立即 ++m_tick 越过」的槽——节点滞留整圈（L1 槽 30 边界=900 刻度）才被再
 * 扫。50ms 轮询定时器拍频游走到级联时刻 remaining 恰等于低级轮全跨度（30
 * 刻度）即触发，板上实测 921ms 冻结周期自持振荡。缺陷复现时下述断言的迟到
 * 量 ≈871 拍，远超界；根修后 ≤2 拍（兑现走"已到期直落低级轮"路径固有 +1 拍
 * 加 1 拍余量）。假时钟逐拍驱动，无真实时序依赖，各平台可复现。 */
#define CASC_FIRE_CAP 128
#define CASC_PHASE_COUNT 30       /* 50ms 周期定时器播种相位全覆盖 */
#define CASC_PERIOD_WINDOW 3000   /* 周期扫描驱动窗（刻度） */
#define CASC_PERIOD_INTERVAL 50   /* 判决场景：50ms 轮询定时器 */
#define CASC_PERIOD_MAXGAP 55     /* 根修后触发间隔恒 51 拍；缺陷冻结 ~901 拍 */
#define CASC_PERIOD_MINFIRES 45   /* 窗内健康触发下限（≈3000/51） */
#define CASC_LATE_LIMIT 2         /* L1 毒区单发迟到上限（拍）：兑现固有 +1 加余量 */
#define CASC_L2_LATE_LIMIT 64     /* L2 毒区恢复链（过期→L1→过期→L0）有界迟到上限；
                                   * 缺陷复现时 ≈26100 拍（27s 整圈滞留） */

typedef struct CascFireLog
{
    uint64_t m_fires[CASC_FIRE_CAP]; /**< 每次触发的假时钟读数（=兑现刻度+1）。 */
    int m_count;
    uint64_t m_maxGap;          /**< 相邻触发最大间隔（报告用）。 */
    int64_t m_late;             /**< 单发场景实测迟到（拍，报告用）。 */
} CascFireLog;

static uint64_t s_cascFakeMs;    /* 假时钟（=时间轮刻度，精度 1ms）。 */
static CascFireLog s_cascSingleLogs[3];
static CascFireLog s_cascPeriodicLogs[CASC_PHASE_COUNT];

static uint64_t casc_fake_now(void)
{
    return s_cascFakeMs;
}

static void casc_callback(void* userData, XTimerData* timer)
{
    CascFireLog* log = (CascFireLog*)userData;
    (void)timer;
    if (log && log->m_count < CASC_FIRE_CAP)
        log->m_fires[log->m_count++] = s_cascFakeMs;
}

/* 假时钟步进 1 刻度消费 1 拍，handler 恰推进一个全局刻度。 */
static void casc_drive(XTimeWheelGroup* wheel, uint64_t untilMs)
{
    while (s_cascFakeMs < untilMs) {
        ++s_cascFakeMs;
        XTimeWheelGroup_handler_base(wheel);
    }
}

/* 推进到 m_current_tick ≡ phase (mod mod)，保证毒区场景可精确构造。 */
static void casc_drive_to_phase(XTimeWheelGroup* wheel, unsigned phase, unsigned mod)
{
    while (s_cascFakeMs % mod != phase)
        casc_drive(wheel, s_cascFakeMs + 1);
}

/* 与全局轮同构：精度 1ms + 3 层 ×30 槽（L0=30/L1=900/L2=27000 刻度）。 */
static XTimeWheelGroup* casc_group_create(void)
{
    XTimeWheelGroup* wheel = XTimeWheelGroup_create(1);
    if (!wheel)
        return NULL;
    XTimeWheelGroup_addTimeWheel(wheel, 30);
    XTimeWheelGroup_addTimeWheel(wheel, 30);
    XTimeWheelGroup_addTimeWheel(wheel, 30);
    XTimerGroupBase_setHighResTimeFunc((XTimerGroupBase*)wheel, casc_fake_now);
    s_cascFakeMs = 0;
    return wheel;
}

/* 单发毒区场景：phaseMod-1 相位注册 timeout 单发定时器，令某次级联时刻
 * remaining 恰等于级联源轮一个边界的全局跨度——L1 毒区（timeout=30 首边界
 * 直接入；60 经一次正常落槽后第二边界入）；L2 毒区（900，旧缺陷滞留 27s）。 */
static int casc_single_shot_case(XTimeWheelGroup* wheel, size_t timeoutMs,
                                 unsigned phaseMod, CascFireLog* log,
                                 const char* name, int lateLimit)
{
    int failures = 0;
    uint64_t addTick, expire;
    int64_t late;
    XTimerData data;

    casc_drive_to_phase(wheel, phaseMod - 1, phaseMod);
    addTick = s_cascFakeMs;
    memset(&data, 0, sizeof(data));
    XTimerData_setTimeout(&data, timeoutMs);
    XTimerData_setSingleShot(&data, true);
    XTimerData_setTimerCallback(&data, casc_callback);
    XTimerData_setUserData(&data, log);
    if (!XTimeWheelGroup_addTimerMs_base(wheel, data)) {
        XPrintf("[级联重落槽][%s] 注册失败\n", name);
        return 1;
    }
    expire = addTick + timeoutMs;
    /* 驱动窗覆盖本级整圈滞留（缺陷复现时也必须等到兑现，红有实值）。 */
    casc_drive(wheel, expire + phaseMod * 30 + 64);

    if (log->m_count != 1) {
        XPrintf("[级联重落槽][%s] 失败: 触发 %d 次（预期 1 次）\n",
                name, log->m_count);
        return 1;
    }
    /* fire 记录 = 兑现刻度+1（handler 拍后观测），迟到 = 记录-expire-1。 */
    late = (int64_t)(log->m_fires[0] - expire) - 1;
    log->m_late = late;
    if (late < 0 || late > lateLimit) {
        XPrintf("[级联重落槽][%s] 失败: 迟到 %lld 拍（上限 %d）\n",
                name, (long long)late, lateLimit);
        ++failures;
    }
    return failures;
}

/* 判决场景全景：50ms 周期定时器按 30 个播种相位全覆盖。级联时刻 remaining
 * 由拍频游走决定，相位 p≡0(mod 3) 必走进 remaining==30 毒区——缺陷复现时
 * 出现 ~901 拍冻结间隔；根修后所有相位触发间隔恒 51 拍。 */
static int casc_periodic_case(XTimeWheelGroup* wheel)
{
    int failures = 0;
    int p;
    unsigned seedPhase = (unsigned)(s_cascFakeMs % 30);
    uint64_t windowEnd;

    for (p = 0; p < CASC_PHASE_COUNT; ++p) {
        XTimerData data;
        casc_drive(wheel, s_cascFakeMs + 1); /* 逐拍播种铺开 30 个相位 */
        memset(&data, 0, sizeof(data));
        XTimerData_setInterval(&data, CASC_PERIOD_INTERVAL);
        XTimerData_setSingleShot(&data, false);
        XTimerData_setTimerCallback(&data, casc_callback);
        XTimerData_setUserData(&data, &s_cascPeriodicLogs[p]);
        if (!XTimeWheelGroup_addTimerMs_base(wheel, data)) {
            XPrintf("[级联重落槽][periodic-p%u] 注册失败\n",
                    (unsigned)((seedPhase + p) % 30));
            ++failures;
        }
    }
    windowEnd = s_cascFakeMs + CASC_PERIOD_WINDOW;
    casc_drive(wheel, windowEnd);

    for (p = 0; p < CASC_PHASE_COUNT; ++p) {
        CascFireLog* log = &s_cascPeriodicLogs[p];
        unsigned phase = (unsigned)((seedPhase + p) % 30);
        int j;
        log->m_maxGap = 0;
        for (j = 1; j < log->m_count; ++j) {
            uint64_t gap = log->m_fires[j] - log->m_fires[j - 1];
            if (gap > log->m_maxGap)
                log->m_maxGap = gap;
        }
        if (log->m_count < CASC_PERIOD_MINFIRES) {
            XPrintf("[级联重落槽][periodic-p%u] 失败: 触发 %d 次（下限 %d）\n",
                    phase, log->m_count, CASC_PERIOD_MINFIRES);
            ++failures;
        }
        if (log->m_maxGap > CASC_PERIOD_MAXGAP) {
            XPrintf("[级联重落槽][periodic-p%u] 失败: 最大触发间隔 %llu 拍（上限 %d；整圈滞留缺陷≈901）\n",
                    phase, (unsigned long long)log->m_maxGap, CASC_PERIOD_MAXGAP);
            ++failures;
        }
    }
    return failures;
}

static int casc_regression(void)
{
    int failures = 0;
    XTimeWheelGroup* wheel = casc_group_create();
    if (!wheel) {
        XPrintf("[级联重落槽] 时间轮组创建失败\n");
        return 1;
    }
    XPrintf("---------- 级联重落槽回归（确定性假时钟） ----------\n");
    failures += casc_single_shot_case(wheel, 30, 30, &s_cascSingleLogs[0],
                                      "single-30ms-L1首边界毒区", CASC_LATE_LIMIT);
    failures += casc_single_shot_case(wheel, 60, 30, &s_cascSingleLogs[1],
                                      "single-60ms-L1次边界毒区", CASC_LATE_LIMIT);
    failures += casc_single_shot_case(wheel, 900, 900, &s_cascSingleLogs[2],
                                      "single-900ms-L2毒区", CASC_L2_LATE_LIMIT);
    failures += casc_periodic_case(wheel);
    if (failures == 0)
        XPrintf("[级联重落槽] 通过: 毒区单发实测迟到 %lld/%lld/%lld 拍, 50ms 周期 30 相位间隔≤%d 拍\n",
                (long long)s_cascSingleLogs[0].m_late,
                (long long)s_cascSingleLogs[1].m_late,
                (long long)s_cascSingleLogs[2].m_late,
                CASC_PERIOD_MAXGAP);
    return failures;
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

    /* 确定性段先行：不依赖真实时序，缺陷复现即红。 */
    failures += casc_regression();

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
