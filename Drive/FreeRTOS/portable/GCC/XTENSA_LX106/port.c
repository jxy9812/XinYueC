/*
 * FreeRTOS V9.0.0 port 层 —— ESP8266 LX106 / call0 ABI / NONOS3 底座
 *
 * 中断模型：
 *   - 全部外设中断（含 CCORRECT0 tick inum6、soft inum7）都在 CPU level 1
 *     （core-isa.h: XCHAL_INT0..13_LEVEL=1）。
 *   - 自研 L1 向量（portasm.S）保存 96 字节 XT_STK 帧到任务栈，
 *     调 vPortL1Dispatch 分发，再从 pxCurrentTCB->pxTopOfStack 恢复。
 *   - tick：CCOMPARE0，80000 cycles @80MHz = 1ms。
 *   - yield：vPortYield 置 INTSET bit7 → 同一 L1 路径切换。
 *   - 临界区：rsil 3（关 level1/2，NMI 独立）。
 */

#include <xtensa/config/core.h>
#include <xtensa/tie/xt_interrupt.h>

#include "FreeRTOS.h"
#include "task.h"

typedef unsigned int uint32;

#define ETS_CCOMPARE0_INUM   6
#define ETS_SOFT_INUM        7
#define TICK_CYCLES          ( configCPU_CLOCK_HZ / configTICK_RATE_HZ )  /* 80000 */

/* ---------- 小工具：特殊寄存器访问（均在内核态/中断态执行） ---------- */
static inline uint32 xPortReadInts( void )
{
    uint32 v;
    __asm__ volatile ( "rsr %0, interrupt" : "=a" ( v ) );
    return v;
}
static inline void xPortClearInts( uint32 mask )
{
    __asm__ volatile ( "wsr %0, intclear" :: "a" ( mask ) : "memory" );
}

/* ---------- 任务初始栈（call0 ABI，96B 帧布局见 portasm.S） ---------- */
extern void vPortTaskExit( void );   /* 任务函数返回落点：标记 + 死循环 */

#define XT_STK_PC     0x04
#define XT_STK_PS     0x08
#define XT_STK_A0     0x0C
#define XT_STK_A1     0x10
#define XT_STK_A2     0x14
#define XT_STK_FRMSZ  96

StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack,
                                    TaskFunction_t pxCode,
                                    void *pvParameters )
{
    uint32 *sp, *tp;

    sp = (uint32 *) ( ( (uint32) ( pxTopOfStack + 1 ) - XT_STK_FRMSZ ) & ~0xfu );
    for ( tp = sp; tp <= (uint32 *)pxTopOfStack; ++tp )
        *tp = 0;

    sp[ XT_STK_PC / 4 ] = (uint32) pxCode;              /* 恢复后 jx 的目标 */
    sp[ XT_STK_PS / 4 ] = 0;                            /* 内核态、INTLEVEL=0、EXCM=0 */
    sp[ XT_STK_A0 / 4 ] = (uint32) vPortTaskExit;       /* 任务函数 ret 净化落点 */
    sp[ XT_STK_A1 / 4 ] = (uint32) ( sp + XT_STK_FRMSZ / 4 );  /* 恢复时的 SP */
    sp[ XT_STK_A2 / 4 ] = (uint32) pvParameters;        /* 任务入参 */

    return (StackType_t *) sp;
}

/* ---------- 临界区（rsil，带嵌套计数） ---------- */
static volatile uint32 uxCriticalNesting = 0;
static uint32 uxSavedInterruptPS = 0;

void vPortEnterCritical( void )
{
    if ( uxCriticalNesting++ == 0 ) {
        __asm__ volatile ( "rsil %0, 3" : "=a" ( uxSavedInterruptPS ) :: "memory" );
    }
}

void vPortExitCritical( void )
{
    if ( --uxCriticalNesting == 0 ) {
        __asm__ volatile ( "wsr %0, ps" :: "a" ( uxSavedInterruptPS ) : "memory" );
    }
}

UBaseType_t uxPortSetInterruptMaskFromISR( void )
{
    uint32 ps;
    __asm__ volatile ( "rsil %0, 3" : "=a" ( ps ) :: "memory" );
    return (UBaseType_t) ps;
}

void vPortClearInterruptMaskFromISR( UBaseType_t uxSaved )
{
    __asm__ volatile ( "wsr %0, ps" :: "a" ( (uint32) uxSaved ) : "memory" );
}

/* ---------- yield：置 soft 中断位，L1 路径切换 ---------- */
void vPortYield( void )
{
    xthal_set_intset( 1u << ETS_SOFT_INUM );
}

/* L1 中断分发已迁至 port_isr.c（IRAM）——中断链不得跨 flash 取指 */

/* 供 portasm.S 取 pxCurrentTCB->pxTopOfStack。
 * V9 的 TCB_t 定义私有在 tasks.c 里，外部只见不完整类型 ——
 * 用「pxTopOfStack 是 TCB 首成员」的偏移 0 事实访问（与 SDK _xt_int_enter 一致）。 */
StackType_t *pxPortGetTopOfStackFrame( void );
void vPortSaveTopOfStackFrame( StackType_t *pxFrame );   /* 实现在 port_isr.c（IRAM） */

extern volatile uint32 vPortTickCounter;   /* 实现在 port_isr.c */
extern volatile uint32 vPortL1Entries;
extern volatile uint32 vPortDispatchState;
extern volatile uint32 vPortEpcAtEntry;

/* ---------- 向量表符号 ---------- */
extern char _text_start[];

/* ---------- 启动早期 WDT 关闭（user_pre_init 调用） ----------
 * 本轮实证（_cap_r1..r4）：
 * 1) chip_erase 后 rf_cal 扇区（PHY_DATA=0x3FB000，esp_init_data_default.bin
 *    首字节 0x05）为空 ⇒ SDK 启动链在 rf_cal 检查后走异常路径主动软复位
 *    （rst cause:2，~0.8s 周期循环）。写回 init_data 后消失。
 * 2) 提前切 vecbase（r4 实验）：SDK 启动期自开的 level-1 中断会闯入自研
 *    L1 路径，此时 pxCurrentTCB==NULL，帧回写/恢复踏空 ⇒ 'Z' 后落进
 *    _xt_f_l2 死循环（'2' 洪流打满 74880 波特）。⇒ vecbase 必须维持原设计：
 *    xPortStartScheduler 内（中断使能同一时刻）才切换，SDK 阶段向量归 ROM。
 * 本函数仅保留 ets_wdt_disable：消除 wdt_init(1) 之前的 ROM HW-WDT 噪声。 */
extern void ets_wdt_disable( void );

void vPortEarlyVectors( void )
{
    ets_wdt_disable();
}

/* 启动期标签：直写 UART FIFO（不经 putc 水位逻辑， immune 到任何 putc 异常） */
static void fifo_puts( const char *s )
{
    volatile uint32 *f = (volatile uint32 *) 0x60000000;
    while ( *s ) { *f = (uint32)(unsigned char)*s++; }
}

/* ---------- 启动第一个任务（portasm.S，不返回） ---------- */
extern void vPortStartFirstTask( void );

BaseType_t xPortStartScheduler( void )
{
    /* 关中断，接管中断系统 */
    __asm__ volatile ( "rsil a2, 3" ::: "a2", "memory" );

    /* vecbase → _text_start（SDK 规范基址，自研槽在 +0x10/+0x30/+0x50，
     * 其余槽保持 SDK 原样）。必须在中断使能的同一直径内完成：此后任何
     * L1 中断都进自研路径（pxCurrentTCB 已由 vTaskSwitchContext 填好）。 */
    __asm__ volatile ( "wsr %0, vecbase" :: "a" ( _text_start ) : "memory" );
    __asm__ volatile ( "esync" );

    /* 先装 CCOMPARE0 再开中断（本轮实证 _cap_r5：CCOMPARE0 复位值≈0 <
     * ccount，intenable 一写 tick 立即投递，此时 pxCurrentTCB 尚未由
     * vTaskSwitchContext 填好 ⇒ 分发/恢复 NULL 踏空 ⇒ 乱跳 '2' 洪流。
     * 顺序：装表 → 清 pending → 开 intenable(6,7)。 */
    {
        uint32 now;
        __asm__ volatile ( "rsr %0, ccount" : "=a" ( now ) );
        __asm__ volatile ( "wsr %0, ccompare0" :: "a" ( now + TICK_CYCLES ) : "memory" );
        __asm__ volatile ( "esync" );
    }
    xPortClearInts( ( 1u << ETS_CCOMPARE0_INUM ) | ( 1u << ETS_SOFT_INUM ) );
    /* 本轮根修：只「或上」我们自己的两个源，不再整体覆盖 intenable。
     * 上一版写成 wsr intenable = (6|7)，把 NONOS/WiFi 自己的 level-1 源
     * （真机实测 intenable=0x501，即 bit0/bit8/bit10）全部关掉了——SDK 事件泵
     * 与射频中断从此失能。 */
    {
        uint32 en;
        __asm__ volatile ( "rsr %0, intenable" : "=a" ( en ) );
        en |= ( 1u << ETS_CCOMPARE0_INUM ) | ( 1u << ETS_SOFT_INUM );
        __asm__ volatile ( "wsr %0, intenable" :: "a" ( en ) : "memory" );
    }

    /* 上一版遗留的实验代码「手工置 soft 中断挂起位」已删除：它会在调度器
     * 刚起来的第一拍人为制造一次 level-1 中断，与 SDK 自己的 level-1 源
     * 撞在一起，干扰「谁拥有这次中断」的判读（cap_r2_g.log 的 L=1 就是它）。
     * 需要验证 tick 时看 vPortTickCounter(k=) 与 vPortL1Entries(L=) 两个计数器。 */

    vTaskSwitchContext();
    /* 再关一次 WDT（本轮根因）：SDK 的 call_user_start_local 在直调 user_init
     * 的前一条指令 wdt_init(1) 打开软 WDT（app_main.o 反汇编 +0xf41 实证），
     * 其喂狗点在 ets_run 任务泵——我们劫持 CPU 后无人喂 → NMI → 周期复位。
     * r14/r15 实证：'T' 后静默死亡+快速复位、无任何 KEX/'%'——非异常，
     * 正是 WDT 复位。此处 wdt_init(1) 已过，关闭后无再使能者。 */
    ets_wdt_disable();
    /* 第 3 轮：X9_IPROBE=1 复现第 2 轮的「探针在场即死」，供与活版 elf
     * 做向量区/字面量差分。默认 0（活版）。 */
#if X9_IPROBE
    {
        unsigned ie;
        int i;
        extern void uart0_putc_raw( char c );
        __asm__ volatile ( "rsr %0, intenable" : "=a" ( ie ) );
        uart0_putc_raw( 'I' ); uart0_putc_raw( ':' );
        for ( i = 7; i >= 0; --i ) {
            int d = ( int ) ( ( ie >> ( i * 4 ) ) & 0xF );
            uart0_putc_raw( ( char ) ( d < 10 ? '0' + d : 'A' + d - 10 ) );
        }
        uart0_putc_raw( '\r' ); uart0_putc_raw( '\n' );
    }
#endif
    vPortStartFirstTask();   /* 不返回 */

    return pdTRUE;   /* 不可达 */
}

void vPortEndScheduler( void )
{
    /* 无法返回到调度器启动前的世界 */
}

/* 调度器运行后 vPortYield 所在级别已知；无 V9 API 可在 ISR 用的钩子弱实现 */
void vPortValidateInterruptPriority( void ) __attribute__((weak));
void vPortValidateInterruptPriority( void ) { }

/* ---------- 任务函数返回落点 ---------- */
void vPortTaskExit( void );
void vPortTaskExit( void )
{
    extern void uart0_putc_raw( char c );
    uart0_putc_raw( 'Q' );   /* 任务函数不该返回 */
    for ( ;; ) { }
}

/* ---------- 极简 ISR 表（保留 SDK 兼容符号，v1 无使用者） ---------- */
typedef void (* _xt_isr_t)( void *arg );
static _xt_isr_t vPortIsrTable[16];
void _xt_isr_attach( unsigned i, _xt_isr_t func, void *arg )
{
    if ( i < 16 ) vPortIsrTable[i] = func;
    (void) arg;
}

/* ---------- 内核钩子（app.c 提供 UART 原语） ---------- */
extern void uart0_putc_raw( char c );
extern void uart0_puts( const char* s );

static void vPortFatalMarker( const char* tag )
{
    uart0_puts( "\r\n[esp-v9] " );
    uart0_puts( tag );
    uart0_puts( "\r\n" );
    for ( ;; ) { }
}

void vAssertCalled( const char *pcFileName, unsigned long ulLine )
{
    (void) pcFileName; (void) ulLine;
    vPortFatalMarker( "ASSERT" );
}

void vApplicationStackOverflowHook( void *pxTask, char *pcTaskName )
{
    (void) pxTask; (void) pcTaskName;
    vPortFatalMarker( "STACK-OVF" );
}

/* 分配失败不致命：ESP8266 可用堆只有几十 KB，console 初始化链在低水位时
 * 靠优雅降级跑通（build/arduino_final 已验证），挂死会把降级变成死循环。
 * 打 'M' 标记返回，让调用点自己判空。 */
void vApplicationMallocFailedHook( void )
{
    *( volatile unsigned int * ) 0x60000000 = 'M';
}

/* XThreadStorage_taskCleanup 由 Drive/FreeRTOS/Sync/XThreadFreeRTOS.c 提供
 * （portCLEAN_UP_TCB 钩子的真实实现，登记回收 XThreadData）。自研移植层
 * 里程碑里这里是空实现；合体固件链接了 XThread 模块，保留会撞重复符号。 */

/* ---------- 内核异常诊断 ----------
 * vPortKernelExcDump 已整体迁往 port_isr.c（IRAM）：
 * 上轮实证（r14 周期1）：任务态崩进 '#'（debug 向量）后 dump 无输出，
 * 而洪流引导期能输出——本函数原在 port.c.o（flash），疑似崩时处于
 * cache 不可靠窗口。中断/异常路径的全部代码必须 IRAM。 */
