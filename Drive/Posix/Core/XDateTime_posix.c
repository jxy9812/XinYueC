#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XTime.h"
#include "XDate.h"
#include "XDateTime.h"
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h> /* [CLK 2026-10-29 诊断] getenv。 */
#include <stdint.h> /* [CLK 2026-10-29 诊断] uint32_t 拆位。 */
#include <unistd.h>
// 辅助函数：从 time_t 和 timespec 提取本地 XTime
static XTime create_time_from_timespec(const struct timespec* ts) {
    if (!ts) return XTime_create();

    struct tm local_time;
    time_t raw_sec = ts->tv_sec;
    if (localtime_r(&raw_sec, &local_time) == NULL) {
        return XTime_create();
    }

    // 从纳秒计算毫秒
    int msec = (int)(ts->tv_nsec / 1000000);
    return XTime_create_time(
        local_time.tm_hour,
        local_time.tm_min,
        local_time.tm_sec,
        msec
    );
}

XTime XTime_currentTime(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return XTime_create();
    }
    return create_time_from_timespec(&ts);
}

// 辅助函数：从 time_t 提取本地 XDate
static XDate create_date_from_time_t(time_t raw_sec) {
    struct tm local_time;
    if (localtime_r(&raw_sec, &local_time) == NULL) {
        return XDate_create();
    }
    return XDate_create_date(
        local_time.tm_year + 1900,
        local_time.tm_mon + 1,
        local_time.tm_mday
    );
}

XDate XDate_currentDate(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return XDate_create();
    }
    return create_date_from_time_t(ts.tv_sec);
}

XDateTime XDateTime_currentDateTime(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return XDateTime_create();
    }

    struct tm local_time;
    if (localtime_r(&ts.tv_sec, &local_time) == NULL) {
        return XDateTime_create();
    }

    XDate date = XDate_create_date(
        local_time.tm_year + 1900,
        local_time.tm_mon + 1,
        local_time.tm_mday
    );

    int msec = (int)(ts.tv_nsec / 1000000);
    XTime time = XTime_create_time(
        local_time.tm_hour,
        local_time.tm_min,
        local_time.tm_sec,
        msec
    );

    return XDateTime_create_datetime(date, time);
}

/* -------------------------------------------------------------------------
 * [2026-11-05 纪元口径统一] 毫秒轴与纳秒轴共用一组"一次性纪元锚"
 * -------------------------------------------------------------------------
 * 目标：返回值 = **自 Unix 纪元以来的毫秒/纳秒**，对齐 Qt 的
 * QDateTime::currentMSecsSinceEpoch()；同时差值语义等价于纯单调钟
 * （校时/NTP 跳变不影响超时、限流、定时器）。
 *
 * 做法：只在**首次调用**同时读一次 CLOCK_MONOTONIC 与 CLOCK_REALTIME，
 * 记下 (monoBase, epochBase) 这组锚点，之后一律用单调时钟推进：
 *     ms(t) = epochBaseMs + (monoNs(t) - monoBaseNs) / 1e6
 *     ns(t) = epochBaseNs + (monoNs(t) - monoBaseNs)
 * 两轴由同一组锚点派生，因此 ns/1e6 与 ms 严格同刻度——这一点是刚需：
 * XAbstractEventDispatcher 要把"高精度红黑树截止（ns 轴）"与"时间轮到期
 * （ms 轴 ×1e6）"放在同一个 now 上相减，两轴若各锚各的（历史上 ns 直读
 * REALTIME、ms 读 MONOTONIC）就会在 NTP 校时后错开整段时间。
 *
 * [为什么不再返回 MONOTONIC 裸值] 它的零点是**系统开机**：刚上电的头几百
 * 毫秒就在 0 附近，而消费方普遍把返回值当作"某件事发生的时刻"打戳
 * （XFlowMeter::m_lastChangeMs、XTempHum::m_lastUpdateMs、设备健康
 * lastOkMs…），取到 0 会被当成"没有时刻"——xindustry-flowmeter 的「收数
 * 后有时刻」断言曾 1/5~1/3 概率间歇失败。XTimeWheelGroup / XDeviceTimer
 * 的注释也一直按"纪元时间轴"在写。
 *
 * [局限（有意为之）] 纪元锚只取一次。若系统时钟此后才被 NTP 校准，本函数
 * 的绝对值沿用旧锚（仍单调、仍远大于 0）；需要真实墙钟时刻请用
 * XDateTime_currentWallMSecsSinceEpoch / currentSecsSinceEpoch。
 * ---------------------------------------------------------------------- */
static int64_t s_epochBaseMs = 0;   /* 锚点：Unix 纪元毫秒 */
static int64_t s_epochBaseNs = 0;   /* 锚点：Unix 纪元纳秒（与上同一时刻） */
static int64_t s_monoBaseNs = 0;    /* 锚点：CLOCK_MONOTONIC 纳秒 */
static int s_epochAnchorReady = 0;

/* 同一换算既用于 MONOTONIC 也用于 REALTIME（都是 struct timespec）。 */
static inline int64_t posixTimespecNs(const struct timespec* ts)
{
    return (int64_t)ts->tv_sec * 1000000000LL + (int64_t)ts->tv_nsec;
}

static void posixEnsureEpochAnchor(void)
{
    struct timespec mono;
    struct timespec rt;
    if (s_epochAnchorReady) return;
    if (clock_gettime(CLOCK_MONOTONIC, &mono) != 0) return;
    if (clock_gettime(CLOCK_REALTIME, &rt) != 0) return;
    s_monoBaseNs = posixTimespecNs(&mono);
    s_epochBaseNs = posixTimespecNs(&rt);
    s_epochBaseMs = s_epochBaseNs / 1000000LL;
    s_epochAnchorReady = 1;
}

int64_t XDateTime_currentMSecsSinceEpoch(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return s_epochBaseMs;
    }
    posixEnsureEpochAnchor();
    {
        /* [CLK 2026-10-29 诊断] 刻度源原始读数对账：XGUI_WHEEL_TRACE=1
         * 时每 500ms 至多一条，打印本函数返回的毫秒（64 位拆高低 32 位
         * 用 %lu，绕开板上 libc %zu 错位），并与 MONOTONIC_RAW / REALTIME
         * 同拍对照——判内核 clocksource 失速/回绕/对时干扰。
         * 多线程调用方共享静态量，诊断打点容忍竞争。 */
        static int s_clkEn = -1;
        static int64_t s_clkLastMs = 0;
        int64_t outMs = s_epochBaseMs +
                        (posixTimespecNs(&ts) - s_monoBaseNs) / 1000000LL;
        if (s_clkEn < 0) s_clkEn = getenv("XGUI_WHEEL_TRACE") ? 1 : 0;
        if (s_clkEn && outMs - s_clkLastMs >= 500) {
            struct timespec rawTs;
            struct timespec rtTs;
            int hasRaw = 0;
            int hasRt = (clock_gettime(CLOCK_REALTIME, &rtTs) == 0);
#ifdef CLOCK_MONOTONIC_RAW
            hasRaw = (clock_gettime(CLOCK_MONOTONIC_RAW, &rawTs) == 0);
#endif
            s_clkLastMs = outMs;
            fprintf(stderr, "[CLK] mono_hi=%lu mono_lo=%lu raw_s=%ld raw_ms=%ld rt_s=%ld\n",
                    (unsigned long)(uint64_t)(outMs >> 32),
                    (unsigned long)(uint64_t)(uint32_t)outMs,
                    hasRaw ? (long)rawTs.tv_sec : -1L,
                    hasRaw ? (long)(rawTs.tv_nsec / 1000000) : -1L,
                    hasRt ? (long)rtTs.tv_sec : -1L);
        }
        return outMs;
    }
}

int64_t XDateTime_currentSecsSinceEpoch(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec;
}

/* 墙钟毫秒（CLOCK_REALTIME，含亚秒精度）：currentSecsSinceEpoch 是秒级截断，
 * 无法支撑 NTP 之类需要亚秒精度时间戳的场景；单调钟接口则会在校时时跳变。
 * 本函数补上「真实 UTC 时刻 + 毫秒精度」这一档。 */
int64_t XDateTime_currentWallMSecsSinceEpoch(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

/* 设置系统墙钟时间。仅时间同步（NTP/SNTP 校时）这类必须改动操作系统时间
 * 基准的场景使用；常规时间测量请保持只读。
 * 需要 root 或 CAP_SYS_TIME，权限不足时 settimeofday 返回 EPERM，此处如实
 * 上报 false，让调用方退化为「只算偏移量」而不是静默失败。 */
bool XDateTime_setSystemSecsSinceEpoch(int64_t secs) {
    struct timeval tv;
    if (secs < 0) {
        return false;
    }
    tv.tv_sec = (time_t)secs;
    tv.tv_usec = 0;
    return settimeofday(&tv, NULL) == 0;
}

bool XDateTime_canSetSystemTime(void) {
    /* 不实际写时钟：写权限只能通过一次真实写入或读 /proc/self/status 的
     * CapEff 判定。优先用非侵入的 uid 判定（嵌入式场景通常整体以 root
     * 运行），uid 0 之外的进程再退回尝试一次写入探测。 */
#if defined(__linux__)
    if (geteuid() == 0) {
        return true;
    }
    /* 非 root 时读 /proc/self/status 的 CapEff 位图，判定 CAP_SYS_TIME(25)。 */
    {
        FILE* fp = fopen("/proc/self/status", "r");
        if (fp) {
            char line[256];
            bool granted = false;
            while (fgets(line, (int)sizeof(line), fp)) {
                unsigned long long cap = 0;
                if (sscanf(line, "CapEff:\t%llx", &cap) == 1) {
                    /* CAP_SYS_TIME == 25 */
                    granted = ((cap >> 25) & 1ULL) != 0ULL;
                    break;
                }
            }
            fclose(fp);
            if (granted) {
                return true;
            }
        }
    }
    return false;
#else
    return geteuid() == 0;
#endif
}

int64_t XDateTime_currentNSecsSinceEpoch(void) {
    /* [2026-11-05 纪元口径统一] 与 XDateTime_currentMSecsSinceEpoch 共用
     * 同一组 (monoBase, epochBase) 锚点，由 CLOCK_MONOTONIC 推进：
     *   ns(t) = epochBaseNs + (monoNs(t) - monoBaseNs)
     * 原实现直读 CLOCK_REALTIME：NTP 校时会让 ns 轴跳变，而事件分发器把
     * 本函数当作"高精度定时器截止轴"的 now 使用（见
     * XAbstractEventDispatcher 的双时钟轴归一），跳变会让 Precise 定时器
     * 提前/延后整段时间。同时它还要与 ms 轴（×1e6）同刻度相减，同锚派生
     * 才能保证 ns/1e6 == ms。
     * 需要真实墙钟纳秒请用 XDateTime_currentWallMSecsSinceEpoch 或
     * XDateTime_currentDateTimeUtc。 */
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return s_epochBaseNs;
    }
    posixEnsureEpochAnchor();
    return s_epochBaseNs + (posixTimespecNs(&ts) - s_monoBaseNs);
}

XDateTime XDateTime_currentDateTimeUtc(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return XDateTime_create();
    }

    struct tm utc_time;
    if (gmtime_r(&ts.tv_sec, &utc_time) == NULL) {
        return XDateTime_create();
    }

    XDate date = XDate_create_date(
        utc_time.tm_year + 1900,
        utc_time.tm_mon + 1,
        utc_time.tm_mday
    );

    int msec = (int)(ts.tv_nsec / 1000000);
    XTime time = XTime_create_time(
        utc_time.tm_hour,
        utc_time.tm_min,
        utc_time.tm_sec,
        msec
    );

    return XDateTime_create_datetime(date, time);
}

#endif
