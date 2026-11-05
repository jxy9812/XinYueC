/**
 * @file XTimeAxisEpochTest.c
 * @brief 时间轴「Unix 纪元口径 + 时间轮冷启动对齐」回归测试（2026-11-05）。
 *
 * @details 回归缺陷（两条，互为因果）：
 *          ① 桌面端 XDateTime_currentMSecsSinceEpoch 的零点不是 Unix 纪元：
 *             Win32 以"本进程首次调用 QPC"为原点、POSIX 用 CLOCK_MONOTONIC
 *             裸值（开机）。于是进程刚起来时返回值在 0 附近甚至恒 0，而全库
 *             消费方把它当"某件事发生的时刻"打戳（XFlowMeter::m_lastChangeMs、
 *             XTempHum 的 m_lastUpdateMs、设备健康 lastOkMs…），0 被当成
 *             "没有时刻"——xindustry-flowmeter 的「收数后有时刻」断言曾
 *             1/5~1/3 概率间歇失败。
 *          ② 把零点改纪元后，时间轮的 m_current_tick 初值 0 与真实刻度差
 *             ~1.7e12 拍，原 handler 只能靠"有界追赶"循环逐拍补（单次封顶
 *             max_time/precision 拍）——既要 6e7 次调用才收敛，期间每拍都按
 *             被压缩的时间轴兑现定时器，即**定时器全部立即触发**。
 *
 *          本测试用三段断言把两条都锁住：
 *           A. 双轴同锚：ns/1e6 == ms 严格成立（事件分发器把高精度定时器
 *              截止与时间轮截止放在同一个 now 上相减，依赖这条恒等式）；
 *           B. 纪元刻度：桌面端 ms 落在 2001-09 之后（> 1e12），且两次采样
 *              单调不减；
 *           C. 冷启动对齐：**新建**一个空时间轮（m_clockSynced 未置位），
 *              注册 200ms 定时器后立刻连拍 handler —— 缺陷复现时它会立即
 *              触发（刻度从 0 被瞬间拉到纪元量纲），根修后必须等够 200ms。
 *
 * @note 只依赖 XDateTime / XTimeWheelGroup / XThread 公共接口，不引平台头
 *       文件。裸机后端（ESP8266/STM32）零点仍是 0，段 B 按平台宏跳过。
 *
 * 用法：XTimeAxisEpochTest [观察窗毫秒]（缺省 1200）。退出码 = 失败断言数。
 */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "CXinYueConfig.h"
#include "XDateTime.h"
#include "XTimerData.h"
#include "XTimeWheelGroup.h"
#include "XThread.h"
#include "XPrintf.h"

static int s_failures = 0;

static void check(const char* what, int ok, const char* detail)
{
    if (ok) {
        XPrintf("[PASS] %s\n", what);
    } else {
        XPrintf("[FAIL] %s%s%s\n", what, detail ? " — " : "", detail ? detail : "");
        ++s_failures;
    }
}

/* ---------------- 段 A/B：双轴同锚 + 纪元刻度 ---------------- */
static void testAxisEpoch(void)
{
    int64_t ms, ns;
    int64_t prevMs;
    int i;
    int monotonic = 1;
    char detail[160];

    ms = XDateTime_currentMSecsSinceEpoch();
    ns = XDateTime_currentNSecsSinceEpoch();

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    snprintf(detail, sizeof(detail), "ms=%lld（应为 ~1.7e12）",
             (long long)ms);
    check("毫秒轴落在 Unix 纪元刻度（不是 0 附近的相对量）",
          ms > 1000000000000LL, detail);
#else
    /* 裸机：无 RTC/NTP，零点只能是上电 —— 只断言"非负"。 */
    check("毫秒轴非负（裸机后端零点为上电）", ms >= 0, NULL);
#endif

    {
        int64_t axisDelta = ns / 1000000LL - ms;
        snprintf(detail, sizeof(detail),
                 "ns/1e6 - ms = %lld（容差 ±2000）", (long long)axisDelta);
        /* ns 与 ms 由同一组锚点派生，两者读数相差只在两次 clock_gettime /
         * QPC 调用之间的真实流逝（远小于 2s）。原实现 ns 直读 REALTIME、
         * ms 读 MONOTONIC，差值会是"纪元减开机" ≈ 1.7e12。 */
        check("纳秒轴与毫秒轴同锚（ns/1e6 == ms）",
              axisDelta > -2000 && axisDelta < 2000, detail);
    }

    prevMs = XDateTime_currentMSecsSinceEpoch();
    for (i = 0; i < 2000; ++i) {
        int64_t now;
        XThread_msleep(1);
        now = XDateTime_currentMSecsSinceEpoch();
        if (now < prevMs) { monotonic = 0; break; }
        prevMs = now;
    }
    check("毫秒轴单调不减（2000 次采样）", monotonic, NULL);
}

/* ---------------- 段 C：时间轮冷启动对齐 ---------------- */
typedef struct ColdFireLog {
    int m_count;
    int64_t m_firstMs;
} ColdFireLog;

/* 与全局轮同规的刻度源：框架统一计时源（现为 Unix 纪元毫秒）。 */
static uint64_t wheelEpochMsSource(void)
{
    return (uint64_t)XDateTime_currentMSecsSinceEpoch();
}

static void coldCallback(void* userData, XTimerData* timer)
{
    ColdFireLog* log = (ColdFireLog*)userData;
    (void)timer;
    if (!log) return;
    if (log->m_count == 0)
        log->m_firstMs = XDateTime_currentMSecsSinceEpoch();
    ++log->m_count;
}

static void testWheelColdStart(int64_t windowMs)
{
    /* 与全局轮同构的三级 30 槽轮，但**新建**（m_clockSynced 未置位）：全局轮
     * 在别的测试里可能已跑过几万次 handler，只有新建轮才能复现冷启动路径。 */
    XTimeWheelGroup* wheel = XTimeWheelGroup_create(1);
    ColdFireLog log = { 0, 0 };
    XTimerData data;
    int64_t t0, t1, axisMs, curTick;
    uint64_t firstTickAfter;
    char detail[224];
    const int64_t intervalMs = 200;
    const int coldTicks = 64;   /* 冷启动阶段只连拍这么多拍（不做长时间空转） */

    if (!wheel) {
        check("时间轮创建成功", 0, "XTimeWheelGroup_create 返回 NULL");
        return;
    }
    XTimeWheelGroup_addTimeWheel(wheel, 30);
    XTimeWheelGroup_addTimeWheel(wheel, 30);
    XTimeWheelGroup_addTimeWheel(wheel, 30);
    /* 与生产装配同规：时间轮靠 m_high_res_time_func 取刻度。
     * 全局轮在 XTimeWheelGroup_global_init 里注入 monotonic_msecs_source
     * （= XDateTime_currentMSecsSinceEpoch）；新建轮不注入就**不能**校准
     * 刻度（addTimerNs 直接返回 NULL），故本测试复刻同一注入。 */
    XTimerGroupBase_setHighResTimeFunc((XTimerGroupBase*)wheel,
                                       wheelEpochMsSource);

    /* ---- C1：空轮首拍 handler 必须把刻度对齐到当前时刻 ----
     * 这是纪元化后的核心风险：m_current_tick 初值 0，真实刻度 ~1.7e12。
     * 若没有对齐门，handler 会走"有界追赶"循环——单次封顶
     * max_time/precision = 30*30*30 = 27000 拍，每拍弹槽判到期。既慢又让
     * 时间轴被压缩（后续定时器全部提前兑现）。
     * 断言：首拍之后 m_current_tick 就已落在纪元刻度附近（而不是 27000）。 */
    axisMs = XDateTime_currentMSecsSinceEpoch();
    XTimeWheelGroup_handler_base((XTimerGroupBase*)wheel);
    firstTickAfter = ((XTimerGroupBase*)wheel)->m_current_tick;
    snprintf(detail, sizeof(detail),
             "首拍后 m_current_tick=%llu（时间源 %lld，容差 200ms）",
             (unsigned long long)firstTickAfter, (long long)axisMs);
    check("空轮首拍即把刻度对齐到当前时刻（不逐拍追赶）",
          (int64_t)firstTickAfter >= axisMs - 200 &&
          (int64_t)firstTickAfter <= axisMs + 200, detail);

    /* 连拍几十拍：刻度只应跟着真实时间走（差值 ≈ 拍数），不应跳跃。 */
    curTick = (int64_t)firstTickAfter;
    {
        int i;
        for (i = 0; i < coldTicks; ++i)
            XTimeWheelGroup_handler_base((XTimerGroupBase*)wheel);
    }
    {
        int64_t drift = (int64_t)((XTimerGroupBase*)wheel)->m_current_tick - curTick;
        snprintf(detail, sizeof(detail),
                 "%d 拍后刻度推进 %lld ms（应 ≈ 0，绝不 ≈27000）",
                 coldTicks, (long long)drift);
        check("冷启动后刻度按真实时间推进（无追赶跳跃）",
              drift >= 0 && drift < 5000, detail);
    }

    /* ---- C2：冷启动 + 立即注册的定时器不被提前兑现 ---- */
    memset(&data, 0, sizeof(data));
    XTimerData_setTimeout(&data, (size_t)intervalMs);
    XTimerData_setSingleShot(&data, true);
    XTimerData_setTimerCallback(&data, coldCallback);
    XTimerData_setUserData(&data, &log);

    t0 = XDateTime_currentMSecsSinceEpoch();
    if (!XTimeWheelGroup_addTimerMs_base((XTimerGroupBase*)wheel, data)) {
        check("冷启动轮注册定时器成功", 0, "addTimerMs 返回 NULL");
        XClassDelete(wheel);
        return;
    }

    /* 连拍 handler 直到定时器兑现或超出观察窗。 */
    t1 = t0;
    while (log.m_count == 0 && t1 - t0 < windowMs) {
        XTimeWheelGroup_handler_base((XTimerGroupBase*)wheel);
        XThread_msleep(1);
        t1 = XDateTime_currentMSecsSinceEpoch();
    }

    if (log.m_count == 0) {
        snprintf(detail, sizeof(detail),
                 "%lld ms 观察窗内未兑现（应 ~%lld ms）",
                 (long long)windowMs, (long long)intervalMs);
        check("冷启动轮定时器在观察窗内兑现", 0, detail);
    } else {
        int64_t elapsed = log.m_firstMs - t0;
        snprintf(detail, sizeof(detail),
                 "首拍 %lld ms（标称 %lld ms，容差 -5/+200）",
                 (long long)elapsed, (long long)intervalMs);
        /* 刻度被压成相对量时，200ms 定时器会"早已过期"而首拍即兑现。 */
        check("冷启动轮不提前兑现（刻度对齐到当前时刻）",
              elapsed >= intervalMs - 5 && elapsed <= intervalMs + 200, detail);
        check("冷启动轮只兑现一次（单次定时器）", log.m_count == 1, NULL);
    }

    XClassDelete(wheel);
}

int main(int argc, char** argv)
{
    int64_t windowMs = (argc > 1) ? (int64_t)atoll(argv[1]) : 1200;

    XPrintf("========== 时间轴口径回归（Unix 纪元 + 时间轮冷启动） ==========\n");
    testAxisEpoch();
    testWheelColdStart(windowMs);
    XPrintf("========== %s（失败断言 %d）==========\n",
            s_failures == 0 ? "全部通过" : "存在失败", s_failures);
    fflush(stdout);
    return s_failures;
}
