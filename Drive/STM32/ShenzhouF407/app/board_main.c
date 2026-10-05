/* ==================== 神舟号 STM32F407 XGuiDemo 固件入口 ====================
 * 启动链：Reset_Handler（CMSIS 启动文件）→ SystemInit（168MHz 时钟）→
 * main（外设初始化）→ FreeRTOS 调度 → gui 任务 → xgui_demo_main（永不返回，
 * exec 内自轮询；触摸泵/上屏经 board_xgui_glue 挂接）。
 */
#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "XPrintf.h"
#include "board_sys.h"
#include "board_lcd.h"
#include "board_touch.h"
#include "board_sram.h"
#include "board_mem.h"
#include "board_w25q.h"

/* XGuiDemo 演示入口（xgui_window_demo.c；嵌入式下保留原名不映射 main）。 */
extern int xgui_demo_main(int argc, char* argv[]);

/* heap_4 统计（原型在 heap_4.c 内，无公共头）。 */
extern size_t xPortGetFreeHeapSize(void);
extern size_t xPortGetMinimumEverFreeHeapSize(void);
extern volatile unsigned long g_lastMallocFailSize;
volatile unsigned long g_lastMallocFailSize;

/* newlib 辅助堆边界（.sysheap 段，CCM 16KB）：stdio 缓冲等零星 malloc。 */
/* GUI 任务栈深（字）：XGui 控件树 + 字体渲染 + 演示页（2816×4=11KB）。 */
#ifndef XINYUE_GUI_TASK_STACK
#define XINYUE_GUI_TASK_STACK (2816UL)
#endif
#define XINYUE_GUI_TASK_PRIO 2

/* 面板底色：暂用深蓝（视觉醒目，验证面板驱动后可改回浅灰 0xEF7D）。 */
#define PANEL_BG_COLOR 0x001Fu

static void watch_task(void* argument)
{
    extern volatile unsigned long g_pollCount;
    (void)argument;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(2000));
        XPrintf("[watch] tick=%u poll=%u free=%u\n",
                (unsigned)xTaskGetTickCount(),
                (unsigned)g_pollCount,
                (unsigned)xPortGetFreeHeapSize());
    }
}

static void gui_task(void* argument)
{
    /* 嵌入式默认裁掉 30KB 样式表解析（demo 自带 CLI：--style=fusion 只装
     * Fusion 默认样式，跳过 xgui_demo_theme_css 的规则解析与规则堆分配）。 */
    char* argv[] = { "xgui_demo", "--style=fusion", NULL };
    (void)argument;
    (void)xgui_demo_main(2, argv);
    /* demo 的 exec 在 MCU 上不返回；保险兜底（任务不可返回）。 */
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

int main(void)
{
    size_t sramBytes;
    board_uart1_init();
    board_delay_init();
    /* 注意：sram_init 之前不得调用 XPrintf——XGui 固件的 FreeRTOS 堆在
     * 外扩 SRAM 上，pvPortMalloc 首次调用即 prvHeapInit；若总线未就绪，
     * 堆自由链表被垃圾值污染，首个 vPortFree 即总线故障。 */

    /* 外扩 1MB SRAM 必须最先初始化：FreeRTOS 堆（ucHeap）就在它上面。 */
    if (board_sram_init() && (sramBytes = board_sram_verify()) > 0)
        XPrintf("\n== XinYueC XGuiDemo STM32F407 ==\n"
                "EXT SRAM: %u KB verified\n", (unsigned)(sramBytes / 1024));
    else
        XPrintf("\n== XinYueC XGuiDemo STM32F407 ==\n"
                "[fatal] EXT SRAM not responding\n");

    /* 片内 RAM 的 XMultiPool 注册进 XMemory（XCLASS 默认类型 = MULTIPOOL）。 */
    board_mem_init();
    XPrintf("INT pool: %u KB registered\n",
            (unsigned)(board_mem_intpool_bytes() / 1024));

    board_w25q_init();
    XPrintf("W25Q128 JEDEC: 0x%06X\n", (unsigned)board_w25q_jedec_id());

    board_touch_init();
    board_lcd_init();
    XPrintf("LCD ID: 0x%04X\n", (unsigned)board_lcd_id());
    board_lcd_fill(0, 0, BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT,
                   PANEL_BG_COLOR);
    board_lcd_backlight(1);

    /* 独立看门狗任务：不依赖 GUI 事件循环，每 2s 报告 tick / GUI 轮询
     * 计数 / 堆水位——区分"循环在转"、"被阻塞"、"已挂死"。 */
    extern volatile unsigned long g_pollCount;
    if (xTaskCreate(watch_task, "watch", 512, NULL, 3, NULL) != pdPASS)
        XPrintf("watch task create failed\n");
    else
        XPrintf("watch task created\n");

    if (xTaskCreate(gui_task, "gui", XINYUE_GUI_TASK_STACK, NULL,
                    XINYUE_GUI_TASK_PRIO, NULL) != pdPASS)
    {
        XPrintf("gui task create failed\n");
        for (;;)
            ;
    }
    vTaskStartScheduler();
    for (;;)
        ;
    return 0;
}

/* ------------------------- 缺省异常陷阱 -------------------------
 * 启动文件 Default_Handler 跳进来：读 MSP 顶异常帧的出错 PC/LR 存全局
 * （gdb 停机读 g_faultPC/g_faultLR 即定位根因），再打印 UART。 */
volatile unsigned long g_faultPC;
volatile unsigned long g_faultLR;
volatile unsigned long g_faultSP;

void board_fault_trap(void);

void board_fault_trap(void)
{
    unsigned long msp;
    __asm volatile("mrs %0, msp" : "=r"(msp));
    g_faultSP = msp;
    g_faultPC = *(volatile unsigned long*)(msp + 24);
    g_faultLR = *(volatile unsigned long*)(msp + 20);
    XPrintf("[fatal] Default_Handler pc=%08x lr=%08x sp=%08x",
            g_faultPC, g_faultLR, g_faultSP);
    taskDISABLE_INTERRUPTS();
    for (;;)
        ;
}

/* ------------------------- FreeRTOS 钩子 ------------------------- */
volatile unsigned long g_heapFreeAtFail;
volatile unsigned long g_heapMinEver;

void vApplicationMallocFailedHook(void)
{
    g_heapFreeAtFail = xPortGetFreeHeapSize();
    g_heapMinEver = xPortGetMinimumEverFreeHeapSize();
    XPrintf("[fatal] malloc failed: want=%u free=%u\n",
            (unsigned)g_lastMallocFailSize, (unsigned)g_heapFreeAtFail);
    taskDISABLE_INTERRUPTS();
    for (;;)
        ;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char* name)
{
    (void)task;
    XPrintf("[fatal] stack overflow: %s\n", name ? name : "?");
    taskDISABLE_INTERRUPTS();
    for (;;)
        ;
}

void vAssertCalled(const char* file, unsigned long line)
{
    XPrintf("[fatal] assert: %s:%d\n", file ? file : "?", line);
    taskDISABLE_INTERRUPTS();
    for (;;)
        ;
}
