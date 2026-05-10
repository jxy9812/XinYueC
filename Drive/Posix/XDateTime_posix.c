#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XTime.h"
#include "XDate.h"
#include "XDateTime.h"
#include <time.h>
#include <sys/time.h>
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
#endif