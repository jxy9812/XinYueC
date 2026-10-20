/*
 * 中断路径专用函数 —— 必须 IRAM。
 * 本文件以 port_isr.o（非 *.c.o）编译：生成的 common.ld 里 IRAM 的 .text1
 * 规则 `*(.text.*)` 会吞下 .text.* 段，使本文件全部函数进 IRAM —— 这正是
 * NONOS 对中断处理链的要求（tick 可能在 cache 关闭窗口内触发，
 * flash 取指会被禁止：上一轮 KEX cause=4 epc=flash 地址的教训）。
 */

typedef unsigned int uint32;
typedef unsigned int StackType_t;
#define NULL ((void *)0)

extern void * volatile pxCurrentTCB;

/* ---------- 当前运行任务影子指针（本轮根因修复） ----------
 * V9 的 API 顺序（tasks.c:1224 vTaskDelay）：
 *     vTaskSuspendAll → prvAddCurrentTaskToDelayedList
 *                    → xTaskResumeAll（内部已调 vTaskSwitchContext，
 *                      pxCurrentTCB 此刻已指向"新"任务）
 *                    → portYIELD_WITHIN_API() → vPortYield 置 soft 中断
 * 于是 CPU 仍在跑"旧"任务时，pxCurrentTCB 已指向新任务：中断入口若把
 * 现场写进 pxCurrentTCB，新任务的帧就被旧现场覆盖 → 恢复跳飞。
 * 官方端口（ARM/Xtensa vPortYield）用独立的"当前运行任务"指针解决。
 * 本实现：vPortRunningTcb = CPU 正在运行的任务；帧保存写它，
 * restore 时用 pxCurrentTCB 刷新它。 */
volatile void *vPortRunningTcb = NULL;

void vPortSaveTopOfStackFrame( StackType_t *pxFrame )
{
    *(StackType_t * volatile *) vPortRunningTcb = pxFrame;
}

/* restore 入口调用：开始运行 pxCurrentTCB 指向的任务 */
void vPortSetRunningTcb( void )
{
    vPortRunningTcb = pxCurrentTCB;
}

/* 取当前任务的已存帧（恢复路径用） */
StackType_t *pxPortGetTopOfStackFrame( void );

StackType_t *pxPortGetTopOfStackFrame( void )
{
    return *(StackType_t * volatile *) pxCurrentTCB;
}

/* ---------- 小工具 ---------- */
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

#define ETS_CCOMPARE0_INUM   6
#define ETS_SOFT_INUM        7
#define TICK_CYCLES          80000   /* 80MHz / 1000Hz */

extern long xTaskIncrementTick( void );      /* V9 返回 BaseType_t */
extern void vTaskSwitchContext( void );
extern void system_soft_wdt_feed( void );
extern void uart0_putc_raw( char c );

volatile uint32 vPortTickCounter = 0;   /* 诊断：tick 计数 */
volatile uint32 vPortL1Entries = 0;     /* 诊断：L1 向量入口计数（向量内汇编写入） */
volatile uint32 vPortDispatchState = 0; /* 诊断：1=入口 2=出口 */
volatile uint32 vPortEpcAtEntry = 0;    /* 诊断：向量入口时的 EPC1（汇编写入） */

/* 诊断：进入 KEX 的向量标签（'#' L2 / '?' NMI / 'K' Kernel / '@' User /
 * '%' Double / 'N' 调度器未启动的 L1）。向量桩在裸写 FIFO 失效后改为写这个
 * RAM 标签，由 vPortKernelExcDump 打印——原来裸写只吐出 "KXE" 三个字符，
 * 标签反而丢在断流里。 */
char v9_kex_tag = 0;

/* ---------- L1 中断源归属判定（本轮根修） ----------
 * NONOS 的 WiFi/LWIP 与 ets_timer 也用 level-1 中断（真机 intenable=0x501
 * 即 bit0/bit8/bit10）。上一版自研 L1 向量把这些中断一律吃掉：既没链回
 * SDK 原生 +0x50 分派器（SDK 事件泵就此停摆），vPortL1Dispatch 末尾还会
 * 把它们的 pending 清掉并从 intenable 摘除——等于替 SDK 关掉了自己的中断源。
 * 本版改成「只有 bit6(CCOMPARE0)/bit7(SOFT) 归 V9，其余原样交还 SDK」。
 *
 * 入口在 portasm.S 的 _xt_kex_or_l1(+0x30, RING0 下的 L1 入口) 与
 * _xt_uexc_or_l1(+0x50, RING1 下的 L1 入口)；返回 1=归 V9，0=交还 SDK。 */
#define V9_L1_OWNED_MASK  ( ( 1u << ETS_CCOMPARE0_INUM ) | ( 1u << ETS_SOFT_INUM ) )

static inline uint32 xPortReadIntEnable( void )
{
    uint32 v;
    __asm__ volatile ( "rsr %0, intenable" : "=a" ( v ) );
    return v;
}

uint32 vPortL1OwnedByV9( void )
{
    uint32 live = xPortReadInts() & xPortReadIntEnable();
    /* 无任何挂起 → 不是中断（真异常），交给异常路径 */
    if ( live == 0u ) {
        return 1u;
    }
    /* 只要有一个非 V9 的源挂起且使能，就不归我们管 */
    return ( live & ~(uint32) V9_L1_OWNED_MASK ) ? 0u : 1u;
}

/* 交还 SDK 原生 level-1 分派器：+0x50 槽的 SDK 入口（_UserExceptionVector）。
 * 链接后实测槽位：0x40100050 = 自研 uslot、0x40100054 = _UserExceptionVector
 * （portasm.S 的三条 KEEP 注入各占 3 字节 j，SDK 原向量整体后移 3 字节）。
 * 该入口自带 on-exception 表按 exccause==4 跳到 SDK 的 L1 ISR，并负责
 * 现场保存/恢复与返回被中断代码。 */
void __attribute__((noinline)) vPortSdkL1Handback( void )
{
    for ( ;; ) { ((void (*)(void)) 0x40100057)(); }
}

/* L1 中断分发（由 portasm.S _xt_l1_vector 调用，a2=帧） */
void vPortL1Dispatch( void *pvFrame )
{
    uint32 pend = xPortReadInts();
    (void) pvFrame;
    vPortDispatchState = 1;

    if ( pend & ( 1u << ETS_CCOMPARE0_INUM ) ) {
        uint32 old, now;
        /* 重装 CCORRECT0，落后太多则连装追赶（同 SDK _xt_timer_int 逻辑） */
        do {
            __asm__ volatile ( "rsr %0, ccompare0" : "=a" ( old ) );
            __asm__ volatile ( "wsr %0, ccompare0" :: "a" ( old + TICK_CYCLES ) : "memory" );
            __asm__ volatile ( "esync" );
            __asm__ volatile ( "rsr %0, ccount" : "=a" ( now ) );
        } while ( ( now - old ) > TICK_CYCLES );

        xPortClearInts( 1u << ETS_CCOMPARE0_INUM );

        vPortTickCounter++;
        /* 第 2 轮存活标记：每 1024 拍 ≈1s 裸写一个 'T'（不过锁/格式化），
         * 用来在「任务一个都没跑」的静默态里判别 tick ISR 本身活没活。 */
        if ( ( vPortTickCounter & 0x3FFu ) == 0u ) {
            uart0_putc_raw( 'T' );
        }
        if ( xTaskIncrementTick() != 0 ) {
            vTaskSwitchContext();
        }
    }

    if ( pend & ( 1u << ETS_SOFT_INUM ) ) {
        xPortClearInts( 1u << ETS_SOFT_INUM );
        vTaskSwitchContext();
    }

    /* 本版不再清/摘非 V9 的 pending：那些源由 vPortL1OwnedByV9 在向量入口
     * 就已交还 SDK，这里若再动它们等于二次吞掉 SDK 中断。 */
    vPortDispatchState = 2;
}

/* ---------- 内核异常诊断（逐字符走 uart0_putc_raw） ----------
 * 上一轮铁律：裸 s32i 直写 0x60000000 只吐出 "KXE" 三个字符就断流——UART0
 * TX FIFO 只有 128B，裸写不等水位必然丢字节。app.c:34 uart0_putc_raw 内含
 * TXFIFO_CNT(bits[23:16])>120 的水位轮询，且已被实证可靠（任务态 TASK_B、
 * 向量里的 '#'/'N' 标签都是它打的）。本轮所有 KEX 字符改走它。
 *
 * pvWin  = portasm.S 向量桩在异常入口 spill 的 64B 现场：
 *          a0@[0]  a2@[8]  a3@[12]  a4@[16] ... a15@[60]
 *          原始 a1 = (char*)pvWin + 64（spill 前 a1 已被 addi -64）
 * 异常入口 CPU 不改 windowbase，且 call0 不开窗 ⇒ 桩里直接 mov/s32i 读到的
 * a0..a15 就是被中断代码的窗口寄存器。 */
extern void uart0_putc_raw( char c );

static void khex( uint32 v )
{
    int i;
    for ( i = 7; i >= 0; i-- ) {
        uint32 n = ( v >> ( i * 4 ) ) & 0xF;
        uart0_putc_raw( (char) ( ( n < 10 ) ? ( '0' + n ) : ( 'A' + n - 10 ) ) );
    }
    uart0_putc_raw( ' ' );
}

static void kstr( const char *s )
{
    while ( *s ) uart0_putc_raw( *s++ );
}

static void klab( char c )
{
    uart0_putc_raw( c );
    uart0_putc_raw( '=' );
}

void vPortKernelExcDump( void *pvWin )
{
    volatile uint32 *w = (volatile uint32 *) pvWin;
    uint32 reg[16];
    uint32 cause, epc, psr, vecb, ien, pend, cc0, ccount, excva;
    int i;

    __asm__ volatile ( "rsr %0, exccause"  : "=a" ( cause ) );
    __asm__ volatile ( "rsr %0, epc1"      : "=a" ( epc ) );
    __asm__ volatile ( "rsr %0, ps"        : "=a" ( psr ) );
    __asm__ volatile ( "rsr %0, excvaddr"  : "=a" ( excva ) );
    __asm__ volatile ( "rsr %0, vecbase"   : "=a" ( vecb ) );
    __asm__ volatile ( "rsr %0, intenable" : "=a" ( ien ) );
    __asm__ volatile ( "rsr %0, interrupt" : "=a" ( pend ) );
    __asm__ volatile ( "rsr %0, ccompare0" : "=a" ( cc0 ) );
    __asm__ volatile ( "rsr %0, ccount"    : "=a" ( ccount ) );

    reg[0] = w[0];                 /* a0  */
    reg[1] = *(volatile uint32 *)((volatile char *)pvWin + 64); /* a1 */
    reg[2] = w[2];                 /* a2  */
    reg[3] = w[3];                 /* a3  */
    for ( i = 4; i < 16; i++ ) reg[i] = w[i];

    kstr( "\r\nKEX " );
    uart0_putc_raw( v9_kex_tag ? v9_kex_tag : '?' );
    kstr( "\r\n" );
    klab( 'c' ); khex( cause );   uart0_putc_raw( '\r' ); uart0_putc_raw( '\n' );
    klab( 'e' ); khex( epc );     /* epc1  → objdump 反查符号 */
    klab( 'v' ); khex( excva );   /* EXCVADDR */
    klab( 'p' ); khex( psr );     uart0_putc_raw( '\r' ); uart0_putc_raw( '\n' );
    klab( 'b' ); khex( vecb );
    klab( 'i' ); khex( ien );
    klab( 'n' ); khex( pend );
    klab( 'w' ); khex( cc0 );
    klab( 't' ); khex( ccount );
    uart0_putc_raw( '\r' ); uart0_putc_raw( '\n' );
    for ( i = 0; i < 16; i++ ) {
        /* 标签 A0..A9 / B0..B5 = a0..a9 / a10..a15 */
        klab( (char) ( 'A' + ( i / 10 ) ) );
        uart0_putc_raw( (char) ( '0' + ( i % 10 ) ) );
        uart0_putc_raw( ':' );
        khex( reg[i] );
        if ( i == 7 ) uart0_putc_raw( '\r' );
    }
    uart0_putc_raw( '\r' ); uart0_putc_raw( '\n' );
    klab( 'L' ); khex( vPortL1Entries );
    klab( 'k' ); khex( (uint32) vPortTickCounter );
    klab( 'r' ); khex( (uint32) vPortRunningTcb );
    klab( 'x' ); khex( (uint32) pxCurrentTCB );
    uart0_putc_raw( '\r' ); uart0_putc_raw( '\n' );
    kstr( "KEXEND\r\n" );
    for ( ;; ) { }
}

/* ---------- 向量入口 gate（供 portasm 的 +0x30/+0x50 stub 调用） ----------
 * 调度器未启动（pxCurrentTCB==NULL）时把异常/中断交还 SDK 原生路径：
 *   +0x30 → 0x40100034（SDK kernel stub = break 兜底）
 *   +0x50 → 0x40100054（_UserExceptionVector 入口；幂等重做 wsr excsave1）
 * IRAM。noinline 防合并。 */
void __attribute__((noinline)) vPortKexGate( void )
{
    if ( pxCurrentTCB == NULL ) {
        for ( ;; ) { ((void (*)(void)) 0x40100034)(); }
    }
}

void __attribute__((noinline)) vPortUserGate( void )
{
    if ( pxCurrentTCB == NULL ) {
        for ( ;; ) { ((void (*)(void)) 0x40100057)(); }
    }
}

/* 供 portasm 判断调度器是否已启动（call0 返回值在 a2）。IRAM。 */
void *vPortTcbValue( void )
{
    return pxCurrentTCB;
}
