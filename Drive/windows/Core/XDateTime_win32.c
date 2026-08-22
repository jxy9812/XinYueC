#ifdef _WIN32
#include"XDate.h"
#include"XTime.h"
#include"XDateTime.h"
#include <windows.h>
#include <time.h>
XDate XDate_currentDate(void) {
    SYSTEMTIME st;
    GetLocalTime(&st); // 获取本地时间
    return XDate_create_date(
        (int)st.wYear,
        (int)st.wMonth,
        (int)st.wDay
    );
}
XTime XTime_currentTime(void) {
    SYSTEMTIME st;
    GetLocalTime(&st); // 获取本地时间
    return XTime_create_time(
        (int)st.wHour,
        (int)st.wMinute,
        (int)st.wSecond,
        (int)st.wMilliseconds // 直接使用毫秒字段
    );
}
XDateTime XDateTime_currentDateTime(void) {
    SYSTEMTIME st;
    GetLocalTime(&st); // 获取本地时间

    XDate date = XDate_create_date(
        (int)st.wYear,
        (int)st.wMonth,
        (int)st.wDay
    );

    XTime time = XTime_create_time(
        (int)st.wHour,
        (int)st.wMinute,
        (int)st.wSecond,
        (int)st.wMilliseconds // 直接使用毫秒字段
    );

    return XDateTime_create_datetime(date, time);
}
#define EPOCH_DIFFERENCE_IN_100NS (116444736000000000ULL)

XDateTime XDateTime_currentDateTimeUtc(void) {
    // 我们可以复用 GetSystemTimeAsFileTime 的结果来构建 XDateTime，
    // 但为了保持接口清晰和简单，这里继续使用 GetSystemTime。
    // 因为 XDateTime_currentDateTimeUtc 的主要用途是提供一个完整的日期时间对象，
    // 而非用于高性能时间戳获取。
    SYSTEMTIME st;
    GetSystemTime(&st);

    XDate date = XDate_create_date(
        (int)st.wYear,
        (int)st.wMonth,
        (int)st.wDay
    );

    XTime time = XTime_create_time(
        (int)st.wHour,
        (int)st.wMinute,
        (int)st.wSecond,
        (int)st.wMilliseconds
    );

    return XDateTime_create_datetime(date, time);
}
static uint64_t getUnixTimeIn100Ns(void) {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft); // 统一使用高精度 API

    // 合并高低32位为一个64位整数（单位：100纳秒）
    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;

    // 转为自 Unix 纪元以来的 100 纳秒数
    return ull.QuadPart - EPOCH_DIFFERENCE_IN_100NS;
}
int64_t XDateTime_currentNSecsSinceEpoch(void)
{
    uint64_t since_unix_100ns = getUnixTimeIn100Ns();
    return (int64_t)(since_unix_100ns * 100); // 100ns * 100 = 1ns
}

int64_t XDateTime_currentMSecsSinceEpoch(void) {
    uint64_t since_unix_100ns = getUnixTimeIn100Ns();
    return (int64_t)(since_unix_100ns / 10000); // 10,000 * 100ns = 1ms
}

int64_t XDateTime_currentSecsSinceEpoch(void) {
    uint64_t since_unix_100ns = getUnixTimeIn100Ns();
    return (int64_t)(since_unix_100ns / 10000000); // 10,000,000 * 100ns = 1s
}

#endif