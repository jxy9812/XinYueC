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
    /* 单调时钟（与 posix 后端 CLOCK_MONOTONIC 语义对齐——本函数是全库
     * 统一"当前毫秒"计时源，消费方全部是时长/超时/限流测量，墙钟跳变
     * 会破坏这些测量。需要墙钟日期时间的调用方走 XDateTime_currentDateTime）。
     *
     * [CLK 2026-10-30] 原实现用 GetTickCount64()，但它的粒度是**系统时钟
     * 中断周期 15.625ms**（64Hz），与 POSIX 的 CLOCK_MONOTONIC（1ms 粒度）
     * 不一致 ⇒ Windows 上框架的所有毫秒级行为（时间轮 tick、串口 2ms
     * 轮询拍、应答超时、UI 限频）被整体量化到 15.625ms：
     *   真机 485 联调实测：主站在途时延 100% 落在 15.625ms 的整数倍上
     *   （16/31/125/141/156/172/188/203/219/234ms…），而同一条链路用
     *   pyserial 直连只有 2.9ms ⇒ 差值全在「计时源粒度」。
     * 改用 QueryPerformanceCounter：恒定频率、纳秒分辨率、不受校时影响，
     * 与 POSIX 侧粒度对齐（跨设备行为统一）。 */
    static LARGE_INTEGER frequency = {0, 0};
    static LARGE_INTEGER origin = {0, 0};
    LARGE_INTEGER now;
    if (frequency.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart == 0)
            return (int64_t)GetTickCount64(); /* 取不到频率则回落旧源 */
        QueryPerformanceCounter(&origin);
    }
    if (!QueryPerformanceCounter(&now)) return (int64_t)GetTickCount64();
    return (int64_t)(((now.QuadPart - origin.QuadPart) * 1000) /
                     frequency.QuadPart);
}

int64_t XDateTime_currentSecsSinceEpoch(void) {
    uint64_t since_unix_100ns = getUnixTimeIn100Ns();
    return (int64_t)(since_unix_100ns / 10000000); // 10,000,000 * 100ns = 1s
}

/* 墙钟毫秒（系统实时时钟，含亚秒精度）：补「真实 UTC 时刻 + 毫秒精度」档，
 * 供 NTP 等需要亚秒精度时间戳的场景。会随校时跳变，超时测量勿用。 */
int64_t XDateTime_currentWallMSecsSinceEpoch(void) {
    uint64_t since_unix_100ns = getUnixTimeIn100Ns();
    return (int64_t)(since_unix_100ns / 10000); // 10,000 * 100ns = 1ms
}

/* 设置系统墙钟时间（时间同步校时用）。
 * SetSystemTime 需要 SE_SYSTEMTIME_NAME 特权，普通桌面进程默认没有；未显式
 * 启用时调用会失败（ERROR_NOT_ALL_ASSIGNED / ERROR_PRIVILEGE_NOT_HELD），
 * 此处如实上报 false 让调用方退化为只读模式。
 * 取值用 UTC：SetSystemTime 接收的是 UTC，而 GetSystemTime 返回本地时间，
 * 两者不可混用。 */
bool XDateTime_setSystemSecsSinceEpoch(int64_t secs) {
    FILETIME ft;
    ULARGE_INTEGER ull;
    SYSTEMTIME st;

    if (secs < 0) {
        return false;
    }
    /* FILETIME 以 1601-01-01 为纪元、100ns 为单位。Unix 纪元偏移 11644473600 秒。 */
    ull.QuadPart = (ULONGLONG)(secs + 11644473600LL) * 10000000ULL;
    ft.dwLowDateTime = ull.LowPart;
    ft.dwHighDateTime = ull.HighPart;
    if (!FileTimeToSystemTime(&ft, &st)) {
        return false;
    }
    return SetSystemTime(&st) != 0;
}

bool XDateTime_canSetSystemTime(void) {
    HANDLE token = NULL;
    LUID luid;
    TOKEN_PRIVILEGES tp;
    bool granted = false;

    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }
    if (LookupPrivilegeValue(NULL, SE_SYSTEMTIME_NAME, &luid)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL)) {
            /* AdjustTokenPrivileges 即使部分失败也返回非零，必须查 GetLastError */
            granted = (GetLastError() == ERROR_SUCCESS);
        }
    }
    CloseHandle(token);
    return granted;
}

#endif