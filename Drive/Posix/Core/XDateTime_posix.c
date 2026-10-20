#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XTime.h"
#include "XDate.h"
#include "XDateTime.h"
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
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

int64_t XDateTime_currentMSecsSinceEpoch(void) {
    /* 单调时钟（CLOCK_MONOTONIC，不受对时/跳变影响）：本函数是全库
     * 统一的"当前毫秒"计时源，消费方全部是时长/超时/限流测量
     * （定时器轮/IO 超时/双击去伪/present 限频），墙钟跳变会破坏
     * 这些测量。需要墙钟日期时间的调用方走 XDateTime_currentDateTime
     * /currentSecsSinceEpoch（见约束文档「外部依赖约束」时间源规则）。 */
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
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
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000000000 + (int64_t)ts.tv_nsec;
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
