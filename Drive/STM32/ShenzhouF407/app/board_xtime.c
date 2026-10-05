/* ==================== 神舟号 XDateTime 时间源适配 ====================
 * 库内定时器后端是双时钟轴设计（见 XAbstractEventDispatcher 等待超时
 * 计算）：currentMSecsSinceEpoch = 单调毫秒轴（全局时间轮 Coarse）、
 * currentNSecsSinceEpoch = 墙钟纳秒轴（HrTimerGroup Precise，量纲 ≥1e18
 * 才会被按墙钟轴归一）。本适配按此约定供给：
 * - MSecs：FreeRTOS tick 单调毫秒；
 * - NSecs：固定纪元锚（2023-01-01）+ tick 推进——量纲合规的单调"墙钟"，
 *   两轴差 ~9 个数量级，等待超时计算正确归一。
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
    /* 必须与 Coarse 轴同源单调且从 0 起步：XHrTimerGroup_handler 首轮
     * `while (tick > m_current_tick) tick_base()` 会逐 tick 空转追赶——
     * 若 ns 从墙钟量纲（1.7e18）起步，追赶循环即 1.7e12 次（实测挂死）。
     * ms*1e6 使 tick=ms，有界。 */
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
