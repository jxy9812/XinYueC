/* ==================== 神舟号 XDateTime 时间源适配 ====================
 * [2026-11-05 纪元口径统一] 桌面端（Win32/POSIX）的
 * XDateTime_currentMSecsSinceEpoch 已改为 **Unix 纪元毫秒**（单调源推进 +
 * 首次调用时取一次墙钟锚）。本板无 RTC 电池也无 NTP，给不出纪元基准，
 * 因此**本后端保持"调度启动起的单调毫秒"**——这是有意的跨平台差异，不是
 * 遗漏：
 *  - 时间轮（Coarse）已加冷启动对齐门（XTimeWheelGroup.c 的
 *    m_clockSynced）：首次 handler / 首次 addTimer 时把刻度直接对齐到当前
 *    读数，因此零点在哪并不影响定时器行为；
 *  - 设备数据打戳（lastChangeMs 等）在本板上从 0 附近起，判定"有没有收到
 *    数据"必须用业务对象的 bool 接口，不能写 `ms > 0`（见 XDateTime.h）。
 *
 * 若将来接上 RTC/NTP，只需在下面 XDateTime_currentMSecsSinceEpoch 里加一次性
 * 纪元锚（epochBase + tick 增量），其余框架代码无需改动。
 *
 * 库内定时器后端是双时钟轴设计（见 XAbstractEventDispatcher 等待超时
 * 计算）：currentMSecsSinceEpoch = 单调毫秒轴（全局时间轮 Coarse）、
 * currentNSecsSinceEpoch = 纳秒轴（HrTimerGroup Precise）。两轴必须由**同一
 * 单调源**派生（本板由 ms × 1e6），否则事件分发器把"高精度截止"与"时间轮
 * 截止"放在同一个 now 上相减时会差出固定偏移，等待超时算错。
 */
#include "XDateTime.h"
#include "XDate.h"
#include "XTime.h"
#include "FreeRTOS.h"
#include "task.h"

int64_t XDateTime_currentMSecsSinceEpoch(void)
{
    return (int64_t)xTaskGetTickCount() * (int64_t)portTICK_PERIOD_MS;
}

int64_t XDateTime_currentNSecsSinceEpoch(void)
{
    /* 必须与 Coarse 轴**同源**且量纲与起点一致：XHrTimerGroup 的
     * m_expire_time_ns 由本函数加超时得到，与 XAbstractEventDispatcher 的
     * 双轴归一按 1e18 分界选 now 轴——本板两轴都远小于 1e18，统一走 ms 轴
     * 换算，故 ns 必须严格等于 ms × 1e6。
     * [历史注记] 曾担心 XHrTimerGroup_handler 逐 tick 空转追赶：该行为已由
     * XTimeWheelGroup 的冷启动对齐门取代（见其注记），本板不存在该风险。 */
    return XDateTime_currentMSecsSinceEpoch() * 1000000;
}

int64_t XDateTime_currentSecsSinceEpoch(void)
{
    return XDateTime_currentMSecsSinceEpoch() / 1000;
}

/* ---------------- 日历时间（无 RTC 电池语义：锚定 2026-01-01） -------- */
XDate XDate_currentDate(void)
{
    return XDate_create_date(2026, 1, 1);
}

static XDateTime board_fixed_datetime(void)
{
    XDate d = XDate_create_date(2026, 1, 1);
    XTime t = XTime_create_time(0, 0, 0, 0);
    return XDateTime_create_datetime(d, t);
}

XDateTime XDateTime_currentDateTime(void)
{
    return board_fixed_datetime();
}

XDateTime XDateTime_currentDateTimeUtc(void)
{
    return board_fixed_datetime();
}
