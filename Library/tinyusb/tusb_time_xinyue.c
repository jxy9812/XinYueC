/**
 * @file       tusb_time_xinyue.c
 * @brief      TinyUSB 时间基准对接 XinYueC XDateTime。
 * @details    覆盖 TinyUSB 的两个弱符号时间函数：
 *             - tusb_time_millis_api()  → XDateTime_currentMSecsSinceEpoch()
 *             - tusb_time_delay_ms()  → 基于 XDateTime 的忙等
 *
 * 关于"阻塞延迟"的说明：
 *
 * TinyUSB 中 tusb_time_delay_ms_api() 只在以下场景被调用：
 *   1. 硬件初始化阶段：USB PHY 上电等待、复位后稳定等待
 *      （1~50ms 级别，例如 DWC2 reset、OHCI power-on、STM32 FSDEV 复位）
 *   2. 极少数早期 port 驱动的临时写法
 *
 * 运行时（枚举状态机、端点传输、超时检测）全部使用
 * tusb_time_millis_api() + call_after 异步调度，不阻塞。
 *
 * 因此初始化阶段短暂阻塞是安全的：此时事件循环尚未启动，
 * 设备还未连入，阻塞不影响事件处理。
 *
 * 注意：
 *   - XDateTime 返回墙上时间（epoch ms），不是严格单调时间。
 *     有 NTP 校时的系统上可能导致超时异常。
 *   - 无 NTP、无人为修改时间的嵌入式场景可安全使用。
 *   - 如有严格单调时钟（如 SysTick 计数器），建议替换本文件。
 *
 * 启用方式：嵌入式构建时设置 TINYUSB_USE_XINYUE_DATETIME=ON。
 */
#include "tusb.h"
#include "XData/XDateTime/XDateTime.h"

/* ------------------------------------------------------------------
 * 毫秒级时间戳
 * ------------------------------------------------------------------ */

uint32_t tusb_time_millis_api(void)
{
    /* 截断到 32 位：TinyUSB 只比较时间差，
       49 天溢出一次，对 USB 超时完全够用。 */
    return (uint32_t)XDateTime_currentMSecsSinceEpoch();
}

/* ------------------------------------------------------------------
 * 毫秒级阻塞延迟（仅初始化阶段使用，运行时不会调用）
 * ------------------------------------------------------------------ */

void tusb_time_delay_ms_api(uint32_t ms)
{
    uint32_t start = tusb_time_millis_api();
    while (tusb_time_millis_api() - start < ms) {
        /* 初始化阶段短延迟，忙等即可。
           如果后续发现有运行时调用路径，应改为事件循环 yield。 */
    }
}
