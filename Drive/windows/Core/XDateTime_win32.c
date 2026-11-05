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
/* -------------------------------------------------------------------------
 * [2026-11-05 纪元口径统一] 毫秒轴与纳秒轴共用一组"一次性纪元锚"
 * -------------------------------------------------------------------------
 * 返回 = **自 Unix 纪元以来的毫秒/纳秒**，对齐 Qt 的
 * QDateTime::currentMSecsSinceEpoch()；差值语义等价于纯单调钟（校时/NTP
 * 跳变不影响超时、限流、定时器）。
 *
 * 做法：只在**首次调用**同时取一次 QPC 与系统墙钟，记下
 * (qpcOrigin, epochBase) 锚点，之后纯 QPC 推进：
 *     ms(t) = epochBaseMs + (qpc(t) - qpcOrigin) / freq * 1000
 *     ns(t) = epochBaseNs + (qpc(t) - qpcOrigin) / freq * 1e9
 * 两轴同锚派生，ns/1e6 == ms 严格成立——这是刚需：XAbstractEventDispatcher
 * 要把"高精度红黑树截止（ns 轴）"与"时间轮到期（ms 轴 ×1e6）"放在同一个
 * now 上相减（见其双时钟轴归一注记）。
 *
 * [为什么不再用"进程第一次调用 QPC"当零点] 那种口径下返回值是**进程内相
 * 对毫秒**，进程刚起来的头几百毫秒就在 0 附近甚至恒 0；而全库消费方都把它
 * 当"某件事发生的时刻"打戳（XFlowMeter::m_lastChangeMs、XTempHum 的
 * m_lastUpdateMs、设备健康 lastOkMs…），0 被当成"没有时刻"——实测
 * xindustry-flowmeter 的「收数后有时刻」断言间歇失败 1/5~1/3。
 * XTimeWheelGroup / XDeviceTimer 的注释也一直按"纪元时间轴"在写。
 *
 * [QPC 不可用的兜底] 取不到频率时退回 GetTickCount64（开机毫秒，单调且
 * 远大于 0）。
 *
 * [为什么不会拖垮时间轮] XTimeWheelGroup 的刻度两侧都按 size_t 取值，并带
 * "只前进不回拨"护栏与其冷启动对齐门（m_clockSynced，见 handler 注记）：
 * 纪元量纲只抬高绝对值，拍数与差值不变。
 * ---------------------------------------------------------------------- */
static LARGE_INTEGER s_qpcFreq = { 0, 0 };
static LARGE_INTEGER s_qpcOrigin = { 0, 0 };
static int64_t s_epochBaseMs = 0;
static int64_t s_epochBaseNs = 0;
static int s_epochAnchorReady = 0;

static void win32EnsureEpochAnchor(void)
{
    if (s_epochAnchorReady) return;
    if (!QueryPerformanceFrequency(&s_qpcFreq) || s_qpcFreq.QuadPart == 0) {
        /* 无高精度计数：整体退回开机毫秒轴，ns 轴由 ms 派生。 */
        int64_t tickMs = (int64_t)GetTickCount64();
        s_epochBaseMs = tickMs;
        s_epochBaseNs = tickMs * 1000000LL;
        s_qpcFreq.QuadPart = 0;
        s_epochAnchorReady = 1;
        return;
    }
    QueryPerformanceCounter(&s_qpcOrigin);
    {
        FILETIME ft;
        ULARGE_INTEGER ull;
        GetSystemTimePreciseAsFileTime(&ft);
        ull.LowPart = ft.dwLowDateTime;
        ull.HighPart = ft.dwHighDateTime;
        if (ull.QuadPart >= (ULONGLONG)EPOCH_DIFFERENCE_IN_100NS) {
            uint64_t sinceUnix100ns = ull.QuadPart - (ULONGLONG)EPOCH_DIFFERENCE_IN_100NS;
            s_epochBaseMs = (int64_t)(sinceUnix100ns / 10000ULL);
            s_epochBaseNs = (int64_t)(sinceUnix100ns * 100ULL);
        } else {
            int64_t tickMs = (int64_t)GetTickCount64();
            s_epochBaseMs = tickMs;
            s_epochBaseNs = tickMs * 1000000LL;
        }
    }
    s_epochAnchorReady = 1;
}

/* QPC 起点之后的双轴增量（纳秒）。QPC 不可用时返回 0，由锚点兜底。 */
static int64_t win32ElapsedNs(void)
{
    LARGE_INTEGER now;
    if (s_qpcFreq.QuadPart == 0) return 0;
    if (!QueryPerformanceCounter(&now)) return 0;
    return (int64_t)(((now.QuadPart - s_qpcOrigin.QuadPart) * 1000000000LL)
                     / s_qpcFreq.QuadPart);
}

int64_t XDateTime_currentNSecsSinceEpoch(void)
{
    win32EnsureEpochAnchor();
    return s_epochBaseNs + win32ElapsedNs();
}

int64_t XDateTime_currentMSecsSinceEpoch(void) {
    win32EnsureEpochAnchor();
    return s_epochBaseMs + win32ElapsedNs() / 1000000LL;
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