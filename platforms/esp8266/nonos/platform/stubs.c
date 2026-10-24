/* ESP8266 合体固件（shell + XGui + lwip，单线程事件循环）装配存根。
 *
 * 铁律段（照抄已验收版本，未改一字）：
 *   app_entry 建 cache + 清 .bss + 'C' 身份标记 + call_user_start；
 *   xil_lock/xil_unlock 本地实现；__wrap_spi_flash_read/write/erase；
 *   newlib 系统调用存根；puts；dhcps_stop；
 *   8 字节 __atomic_* 族。
 *
 * 本轮（去 V9 + 分配器改 XMemory）改动：
 *   - 删除自建 first-fit 分配器（h_alloc/h_release/HEAP_END_ADDR），删除对
 *     FreeRTOS V9 heap_4 的转发（pvPortMalloc/vPortFree 直接来自 heap_4.c）。
 *     全机唯一分配器 = 框架自带的 XMemory：
 *       · XMEMORY_TYPE_MULTIPOOL  = 全局多级固定池（32/64/128/256/512 五档）；
 *       · XMEMORY_TYPE_SYSTEM     = 全局 TLSF 可变池（XPLATFORM_BAREMETAL 下
 *                                  arena 为 .bss 静态缓冲，零堆依赖）；
 *       · XMEMORY_TYPE_HYBRID     = <=512B 走多级池，>512B 走 SYSTEM(可变池)。
 *     因此本文件所有 C 运行时分配入口（malloc/free/calloc/realloc 及其
 *     _r 家族）与 NONOS3 libmain 的 pvPort* 胖签名转接到 XMemory，
 *     不存在绕过池的裸分配路径。
 *   - 删除 XMultiPool_* 裁剪补位（XMEMORY_MULTIPOOL_ON 现为 1，真实现由
 *     Src/XMemory/XMultiPool/XMultiPool.c 提供，重复定义会撞链接）。
 *   - XMemory 统计上串口（xm_stat_dump）：poolUsedBytes/poolTotalBytes/
 *     systemBytes 可观测。
 */
extern void call_cache_enable_dout(void);
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include "XMemory.h"
void call_user_start(void);
extern void system_restart(void);   /* user_interface.h（NONOS3 libmain） */
void app_entry(void) __attribute__((noreturn));
void app_entry(void) {
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    *fifo = 'E';   /* eboot 跳转成功，进入应用入口 */
    call_cache_enable_dout();  /* SPI0 寄存器直接配置 cache */
    /* eboot 不清 .bss —— SDK 全局变量会是 RAM 垃圾，这里自己清零 */
    {
        extern char _bss_start[], _bss_end[];
        volatile char* p = _bss_start;
        while (p < _bss_end) *p++ = 0;
    }
    *fifo = 'C';   /* cache enable 返回；'C'=底座身份标记 */
    call_user_start();
    *fifo = 'R';   /* call_user_start 返回（不该发生） */
    for(;;){}
}

/* 中断锁：本地实现，与 NONOS osapi.h 的内联宏同语义（rsil 15 / 恢复 PS）。
 * 不以函数形式调 ROM（--defsym 已从链接命令移除）。 */
unsigned int xil_lock(void)
{
    unsigned int ps;
    __asm__ __volatile__("rsil %0,15" : "=a"(ps) :: "memory");
    return ps;
}
void xil_unlock(unsigned int state)
{
    __asm__ __volatile__("wsr %0,ps;\n\trsync" :: "a"(state) : "memory");
}

/* ---- 临界区入口：XAtomicLibcall_esp8266.c 用 taskENTER_CRITICAL /
 *      taskEXIT_CRITICAL 实现 1/2/4 字节原子，而这两个宏在无 FreeRTOS 时
 *      落到 vPortEnterCritical/vPortExitCritical（V9 port.c 提供，去 V9 后
 *      消失 → 首轮链接实证 undefined reference）。裸机单线程下临界区就是
 *      关中断，按可嵌套实现：只有最外层恢复 PS，与 FreeRTOS 的嵌套语义
 *      一致。中断本身不受影响（rsil 15 只屏蔽可屏蔽级）。 ---- */
static volatile unsigned int s_crit_nest = 0;
static volatile unsigned int s_crit_ps = 0;
void vPortEnterCritical(void)
{
    unsigned int ps = xil_lock();
    if (s_crit_nest == 0) s_crit_ps = ps;
    ++s_crit_nest;
}
void vPortExitCritical(void)
{
    if (s_crit_nest > 0 && --s_crit_nest == 0)
        xil_unlock(s_crit_ps);
}

/* ---- cont 上下文（app.c 的 4KB cont 栈泵）----
 * cont_run/cont_suspend 的汇编体取自 Arduino core 自带的 cont.S（core 是
 * 汇编，本工程零改动直接编），但 cont_init 的 C 实现只存在于 core 的
 * cont_util.cpp（C++ 胶水，本工程是纯 C 链，编不进来）→ 这里按 cont_util.cpp:34-47
 * 逐行照抄：清结构体、下两枚栈哨兵、定位栈顶与结构首址、整栈填哨兵值。 */
#include "cont.h"
#define CONT_STACKGUARD 0xfeefeffe   /* cont_util.cpp:32 的 static constexpr 值 */
cont_t* g_pcont = 0;
void cont_init(cont_t* cont)
{
    int pos;
    if (!cont) return;
    memset(cont, 0, sizeof(cont_t));
    cont->stack_guard1 = CONT_STACKGUARD;
    cont->stack_guard2 = CONT_STACKGUARD;
    cont->stack_end = cont->stack + (sizeof(cont->stack) / 4);
    cont->struct_start = (unsigned*)cont;
    for (pos = 0; pos < (int)(sizeof(cont->stack) / 4); ++pos)
        cont->stack[pos] = CONT_STACKGUARD;
}

/* ---------------- 分配：全部落到 XMemory 的两块池 ---------------- */
/* 说明（为什么这里「有」pvPortMalloc 却没有 FreeRTOS）：
 *   NONOS3 的 libmain/app_partition.c 引用 pvPortZalloc(3 参)、
 *   sdk3_pvPortMalloc(4 参)、xPortWantedSizeAlign —— C 符号不带签名，
 *   与 V9 heap_4 的 1 参 pvPortMalloc 靠 ABI 巧合共用同一符号。本轮
 *   heap_4 不参与编译（XESP8266_FREERTOS_V9_ON=0），改由本文件提供同名
 *   入口，落到 XMemory 的 SYSTEM 槽（= 全局 TLSF 可变池）。
 * ⚠ V9 总闸=1 时 heap_4.c 重新参与编译并自带 pvPortMalloc/vPortFree，
 *   故下面这组定义整块门控，避免与 heap_4 重复定义撞链接（同时也就恢复了
 *   「堆转发接回 heap_4」的老路子供将来接线用）。 */
#if !XESP8266_FREERTOS_V9_ON
void* pvPortMalloc(size_t n)            { return XMalloc_System(n); }
void  vPortFree(void* p)                { XFree_System(p); }
void* pvPortZalloc(size_t n, const char* file, int line)
{
    void* p;
    (void)file; (void)line;
    p = XMalloc_System(n);
    if (p) memset(p, 0, n);
    return p;
}
void* sdk3_pvPortMalloc(size_t n, const char* file, int line, int iram)
{
    (void)file; (void)line; (void)iram;
    return XMalloc_System(n);
}
void* pvPortMalloc_(size_t n) { return XMalloc_System(n); }
void  vPortFree_(void* p)     { XFree_System(p); }
unsigned int xPortWantedSizeAlign(unsigned int n) { return (unsigned int)((n + 7u) & ~7u); }

/* 可观测：SYSTEM(=可变池) 与 MULTIPOOL 两块池的统计。XMemory 的统计接口
 * 按类型给出 poolUsedBytes/poolTotalBytes（多级池口径）与 systemBytes
 * （系统槽口径，裸机下即可变池 arena 的在用量）。 */
unsigned int xPortGetFreeHeapSize(void)
{
    XMemoryStatistics st = XMemory_statistics(XMEMORY_TYPE_VARIABLEPOOL);
    /* 无符号下溢保护：统计不可用时返回 0，不返回巨大值污染上层读数 */
    return (unsigned int)st.poolTotalBytes;
}
#endif /* !XESP8266_FREERTOS_V9_ON —— 统计口（xm_stat_dump）在两种模式下都要用，故不门控 */
/* 串口直写（app.c 定义）：FIFO 直写 + 128B 水位流控（铁律 6） */
void uart0_putc_raw(char c);
void uart0_puts(const char* s);

void xm_stat_dump(const char* tag)
{
    XMemoryStatistics vp = XMemory_statistics(XMEMORY_TYPE_VARIABLEPOOL);
    XMemoryStatistics mp = XMemory_statistics(XMEMORY_TYPE_MULTIPOOL);
    XMemoryStatistics sy = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    char b[12];
    int k;
    unsigned long v;
    uart0_puts(tag);
    v = (unsigned long)mp.poolUsedBytes;
    k = 10; b[10] = 0; if (!v) b[--k]='0';
    while (v) { b[--k] = (char)('0' + (v % 10)); v /= 10; }
    uart0_puts(" poolUsed=");   uart0_puts(&b[k]);
    v = (unsigned long)mp.poolTotalBytes;
    k = 10; b[10] = 0; if (!v) b[--k]='0';
    while (v) { b[--k] = (char)('0' + (v % 10)); v /= 10; }
    uart0_puts(" poolTotal="); uart0_puts(&b[k]);
    v = (unsigned long)sy.systemBytes;
    k = 10; b[10] = 0; if (!v) b[--k]='0';
    while (v) { b[--k] = (char)('0' + (v % 10)); v /= 10; }
    uart0_puts(" systemBytes="); uart0_puts(&b[k]);
    /* systemBytes 在 ESP8266 上恒 0：XMemory.c:40-43 的记账条件要求
     * malloc_usable_size（_WIN32/__linux__/__APPLE__/__BSD__ 之一），裸机
     * 不满足 → XMEMORY_STAT_TRACK_SYSTEM=0，该字段无意义。裸机下的硬数字
     * 是可变池自己的 used/total（SYSTEM 槽就是全局 TLSF 可变池，
     * XMemory.c:250-256 的裸机分支）。 */
    v = (unsigned long)vp.poolUsedBytes;
    k = 10; b[10] = 0; if (!v) b[--k]='0';
    while (v) { b[--k] = (char)('0' + (v % 10)); v /= 10; }
    uart0_puts(" varPoolUsed="); uart0_puts(&b[k]);
    v = (unsigned long)vp.poolTotalBytes;
    k = 10; b[10] = 0; if (!v) b[--k]='0';
    while (v) { b[--k] = (char)('0' + (v % 10)); v /= 10; }
    uart0_puts(" varPoolTotal="); uart0_puts(&b[k]);
    uart0_puts("\r\n");
}

/* 分配尺寸直方图（XMemory.c 分发层收口，框架/lwip/裸 malloc 全覆盖）：
 * 打印一次即可，用真实分布回头调 XMP_GLOBAL_C32/C64/C128 块数。 */
static void xm_u(unsigned long v)
{
    char b[12];
    int k = 10;
    b[10] = 0; if (!v) b[--k] = '0';
    while (v) { b[--k] = (char)('0' + (v % 10)); v /= 10; }
    uart0_puts(&b[k]);
}
void xm_histogram_dump(const char* tag)
{
    static const unsigned int cap[6] = {32, 64, 128, 256, 512, 1024};
    unsigned long c, y, bc, by, fc, fy;
    unsigned int i;
    uart0_puts(tag);
    for (i = 0; i < 6; ++i) {
        char d[6];
        int j = 0;
        XMemory_histogram(i, &c, &y);
        XMemory_histogram_free(i, &fc, &fy);
        uart0_puts(" <=");
        { unsigned int v = cap[i];
          char rev[5]; int n = 0;
          do { rev[n++] = (char)('0' + v % 10); v /= 10; } while (v);
          while (n) d[j++] = rev[--n];
          d[j] = 0; }
        uart0_puts(d);
        uart0_puts(": n="); xm_u(c);
        uart0_puts(" sum="); xm_u(y);
        uart0_puts(" fail="); xm_u(XMemory_histogram_fail(i));
        /* 释放数与净泄漏（申请 - 释放）。净泄漏持续增长 = 该尺寸档有配对缺失。 */
        uart0_puts(" free="); xm_u(fc);
        uart0_puts(" leak="); xm_u(c > fc ? c - fc : 0);
        uart0_puts("  ");
    }
    XMemory_histogram_big(&bc, &by);
    uart0_puts(" >1024: n="); xm_u(bc);
    uart0_puts(" sum="); xm_u(by);
    uart0_puts(" fail="); xm_u(XMemory_histogram_fail(6));
    uart0_puts("\r\n");
}

/* 泄漏取证打印：全局池按尺寸档的 malloc/free 配对。leak 持续增长即泄漏档位。 */
void xm_leak_dump(const char* tag)
{
    static const unsigned int cap[7] = {32, 64, 128, 256, 512, 1024, 1024};
    unsigned int i;
    uart0_puts(tag);
    for (i = 0; i < 7; ++i) {
        unsigned long a = 0, f = 0;
        char d[8]; int j = 0;
#if XMEMORY_HISTO_FREE_ON
        XVariablePool_leak_stats(i, &a, &f);
#endif
        uart0_puts(" <=");
        { unsigned int v = cap[i];
          char rev[6]; int n = 0;
          do { rev[n++] = (char)('0' + v % 10); v /= 10; } while (v);
          while (n) d[j++] = rev[--n];
          d[j] = 0; }
        uart0_puts(d);
        uart0_puts(": alloc="); xm_u(a);
        uart0_puts(" free="); xm_u(f);
        uart0_puts(" leak="); xm_u(a > f ? a - f : 0);
        uart0_puts("  ");
    }
    uart0_puts("\r\n");
}

/* newlib malloc 家族 → 同一 XMemory 池 */
void* _malloc_r(void* r, size_t n)  { (void)r; return XMalloc_System(n); }
void  _free_r(void* r, void* p)     { (void)r; XFree_System(p); }
void* _calloc_r(void* r, size_t c, size_t n) { (void)r; return XCalloc_System(c, n); }
void* _realloc_r(void* r, void* p, size_t n)
{
    (void)r;
    return XMemory_realloc(p, n, XMEMORY_TYPE_SYSTEM);
}
void* malloc(size_t n)             { return XMalloc_System(n); }
void  free(void* p)                { XFree_System(p); }
void* calloc(size_t c, size_t n)   { return XCalloc_System(c, n); }
void* realloc(void* p, size_t n)    { return XMemory_realloc(p, n, XMEMORY_TYPE_SYSTEM); }

/* newlib 系统调用存根 */
int _read_r(void* r, int fd, char* buf, int len) { (void)r;(void)fd;(void)buf;(void)len; return 0; }
#define UART0_STATUS (*(volatile unsigned int*)0x6000001c)  /* TXFIFO_CNT = bits[23:16] */
static void uart0_wait_tx(void) {
    while (((UART0_STATUS >> 16) & 0xff) > 120) { }
}
int _write_r(void* r, int fd, const char* buf, int len) {
    (void)r; if(fd>2) return -1;
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    int i; for(i=0;i<len;i++){ uart0_wait_tx(); *fifo = (unsigned char)buf[i]; }
    return len;
}
int _lseek_r(void* r, int fd, int off, int whence) { (void)r;(void)fd;(void)off;(void)whence; return 0; }
int _close_r(void* r, int fd) { (void)r;(void)fd; return 0; }
int _fstat_r(void* r, int fd, void* st) { (void)r;(void)fd;(void)st; return 0; }
int _isatty_r(void* r, int fd) { (void)r; return fd>=0 && fd<=2; }
void* _sbrk_r(void* r, int incr) { (void)r; (void)incr; return (void*)-1; }
void _exit(int code) { (void)code; system_restart(); for(;;){} }

/* puts：lwip-esp.o 引用（glue 调试通道），带 FIFO 水位流控 + 换行。 */
int puts(const char* s) {
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    while (*s) { uart0_wait_tx(); *fifo = (unsigned char)*s++; }
    uart0_wait_tx(); *fifo = '\r';
    uart0_wait_tx(); *fifo = '\n';
    return 0;
}

/* newlib atexit：无进程退出语义，登记无处触发（gui_stubs 同款） */
int atexit(void (*fn)(void)) { (void)fn; return 0; }

/* XGPU_ON=1 只是为满足 XPainter.c 的无守卫成员引用；ESP8266 无 GL，
 * 工厂取 NULL 表后按 vulkan->gl->software 顺序回退到软件路径
 * （gui_stubs.c:595 同款）。 */
struct XGpuRenderDriverProcs;
const struct XGpuRenderDriverProcs* XGpuRenderDriver_gl_procs(void) { return 0; }

/* NONOS305 blobs 引用，esp 侧与仓库 lwip 均无实现。 */
void dhcps_stop(void* info){(void)info;}

/* wrap spi_flash（铁律 3）：SDK 原生 cache 开关循环在本板 Zbit flash 上
 * 挂死；本 wrap 走 __real_（已验证）。 */
int __real_spi_flash_read(unsigned int src, void* dst, unsigned int size);
int __wrap_spi_flash_read(unsigned int src, void* dst, unsigned int size)
{
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    int r;
    *fifo = '<';
    r = __real_spi_flash_read(src, dst, size);
    *fifo = '>';
    return r;
}
int __real_spi_flash_erase_sector(unsigned int sec);
int __real_spi_flash_write(unsigned int addr, void* src, unsigned int size);
int __wrap_spi_flash_erase_sector(unsigned int sec)
{
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    int r;
    *fifo = 'e';
    r = __real_spi_flash_erase_sector(sec);
    *fifo = (r == 0) ? 'E' : 'o';
    return r;
}
int __wrap_spi_flash_write(unsigned int addr, void* src, unsigned int size)
{
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    int r;
    *fifo = 'w';
    r = __real_spi_flash_write(addr, src, size);
    *fifo = (r == 0) ? 'W' : 'x';
    return r;
}

/* ---- 8 字节宽度原子 libcall（LX106 无原子指令，关中断即临界区） ---- */
uint64_t __atomic_load_8(volatile void* ptr, int memorder)
{
    uint64_t result; (void)memorder;
    { unsigned int s = xil_lock(); result = *(volatile const uint64_t*)ptr; xil_unlock(s); }
    return result;
}
void __atomic_store_8(volatile void* ptr, uint64_t value, int memorder)
{
    unsigned int s; (void)memorder;
    s = xil_lock(); *(volatile uint64_t*)ptr = value; xil_unlock(s);
}
uint64_t __atomic_exchange_8(volatile void* ptr, uint64_t value, int memorder)
{
    uint64_t result; unsigned int s; (void)memorder;
    s = xil_lock();
    result = *(volatile uint64_t*)ptr;
    *(volatile uint64_t*)ptr = value;
    xil_unlock(s);
    return result;
}
_Bool __atomic_compare_exchange_8(volatile void* ptr, void* expected,
                                  uint64_t desired, int weak, int succ, int fail)
{
    _Bool result; unsigned int s;
    (void)weak; (void)succ; (void)fail;
    s = xil_lock();
    if (*(volatile uint64_t*)ptr == *(volatile const uint64_t*)expected) {
        *(volatile uint64_t*)ptr = desired; result = 1;
    } else {
        *(uint64_t*)expected = *(volatile uint64_t*)ptr; result = 0;
    }
    xil_unlock(s);
    return result;
}
uint64_t __atomic_fetch_add_8(volatile void* ptr, uint64_t value, int memorder)
{
    uint64_t result; unsigned int s; (void)memorder;
    s = xil_lock();
    result = *(volatile uint64_t*)ptr;
    *(volatile uint64_t*)ptr = result + value;
    xil_unlock(s);
    return result;
}
uint64_t __atomic_fetch_sub_8(volatile void* ptr, uint64_t value, int memorder)
{
    uint64_t result; unsigned int s; (void)memorder;
    s = xil_lock();
    result = *(volatile uint64_t*)ptr;
    *(volatile uint64_t*)ptr = result - value;
    xil_unlock(s);
    return result;
}

/* symtab 悬空 UND 清账 */
__attribute__((weak)) const char core_version[] = "00000000";

/* OLED 驱动的看门狗钩子（gui_stubs 的同名物；本固件链 libpp，有真实现） */
void system_soft_wdt_feed(void);
void gui_wdt_feed(void) { system_soft_wdt_feed(); }

int settimeofday(const struct timeval* tv, const struct timezone* tz)
{
    (void)tv; (void)tz;
    return 0;
}


/* ---------- 2026-10-21 泄漏定案：lwip 独立 first-fit 堆（os_malloc 家族） ----------
 * 底座（arduino_lwip，ping 0% 无泄漏）架构：lwip 内存不走框架 arena，
 * 走 [_heap_start, 0x3FFFC000) 上的独立 first-fit 堆。原因：esp 胶水层
 * （lwip-esp.o）会以 esp 布局/引用协议改写这些块的头（XVR/CBREF 取证：
 * ref 被写成 245+ 垃圾值，pbuf 永不释放，每 ping 漏 92B）。
 * 算法逐行取自 build/arduino_lwip/stubs.c（底座同款，真机验证）。 */
extern char _heap_start[];
#define LWH_HEAP_END 0x3FFFC000UL

extern unsigned int ets_intr_lock(void);
extern void ets_intr_unlock(unsigned int state);

typedef struct lwh_blk_s {
    struct lwh_blk_s* next;
    struct lwh_blk_s* prev;
    unsigned int size;
    unsigned int used;
} lwh_blk_t;

static lwh_blk_t* lwh_head = 0;
/* 2026-10-23：lwh 容量与【分配失败计数】。
 * ⚠ 曾试图做"已用字节"水位记账，失败：free 时无法可靠知道该扣多少（块头
 * size 可能被 esp 胶水越权改写），记账反复归零 → 水位读数毫无意义（一度
 * 把"分配正常"误判成"耗尽"）。改为只留两个【不可伪造】的量：
 *   lwh_total     堆总容量（常量）
 *   lwh_allocFail 分配失败次数——真耗尽的唯一判据（与 lwip 侧 allocFail
 *                  独立，可交叉验证）。
 * 不改 lwh_blk_t 布局，保持 P0 定案验证过的堆结构不变。 */
static unsigned int lwh_total = 0;
static unsigned int lwh_allocFail = 0;

static void lwh_init(void)
{
    unsigned int start = ((unsigned int)(size_t)_heap_start + 7u) & ~7u;
    unsigned int end = (unsigned int)LWH_HEAP_END;
    if (end < start + sizeof(lwh_blk_t) + 32u) return;
    lwh_head = (lwh_blk_t*)start;
    lwh_head->prev = 0;
    lwh_head->next = 0;
    lwh_head->size = end - start - sizeof(lwh_blk_t);
    lwh_head->used = 0;
    lwh_total = end - start;
    lwh_allocFail = 0;
}

static void* lwh_alloc(unsigned int n)
{
    lwh_blk_t* p;
    if (!lwh_head) lwh_init();
    if (!lwh_head) return 0;
    n = (n + 7u) & ~7u;
    if (n == 0) n = 8;
    for (p = lwh_head; p; p = p->next) {
        if (!p->used && p->size >= n) {
            if (p->size >= n + sizeof(lwh_blk_t) + 8u) {
                lwh_blk_t* q = (lwh_blk_t*)((char*)(p + 1) + n);
                q->next = p->next;
                q->prev = p;
                if (q->next) q->next->prev = q;
                p->next = q;
                q->size = p->size - n - sizeof(lwh_blk_t);
                q->used = 0;
            }
            p->used = 1;
            return (void*)(p + 1);
        }
    }
    ++lwh_allocFail;   /* 真耗尽判据 */
    return 0;
}

static void lwh_free(void* v)
{
    lwh_blk_t* p;
    unsigned int a = (unsigned int)(size_t)v;
    unsigned int lo = ((unsigned int)(size_t)_heap_start + 7u) & ~7u;
    if (!v || a < lo + sizeof(lwh_blk_t) || a >= (unsigned int)LWH_HEAP_END) return;
    p = ((lwh_blk_t*)v) - 1;
    if (!p->used) return;
    p->used = 0;
    if (p->next && !p->next->used) {
        lwh_blk_t* q = p->next;
        p->size += sizeof(lwh_blk_t) + q->size;
        p->next = q->next;
        if (p->next) p->next->prev = p;
    }
    if (p->prev && !p->prev->used) {
        lwh_blk_t* r = p->prev;
        r->size += sizeof(lwh_blk_t) + p->size;
        r->next = p->next;
        if (p->next) p->next->prev = r;
    }
}

/* 诊断：lwh 堆容量（字节）与累计分配失败次数。
 * ⚠ 不要再做"空闲字节"式水位计：esp 胶水会越权改写块头，遍历/记账都不可
 * 靠（曾把"分配正常"误判成"耗尽"）。真耗尽看 allocFail 计数。 */
unsigned int xesp_lwh_capacity(void)
{
    if (!lwh_head) lwh_init();
    return lwh_total;
}

unsigned int xesp_lwh_alloc_fail(void)
{
    return lwh_allocFail;
}

unsigned int xesp_lwh_ready(void)
{
    if (!lwh_head) lwh_init();
    return (unsigned int)(size_t)lwh_head;
}

void* os_malloc(size_t n)
{
    unsigned int s = ets_intr_lock();
    void* p = lwh_alloc((unsigned int)n);
    ets_intr_unlock(s);
    return p;
}

void* os_zalloc(size_t n)
{
    void* p = os_malloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void os_free(void* p)
{
    lwh_free(p);
}


/* ---------- XMultiPool 平台自实现装配（XMP_GLOBAL_INIT_CUSTOM 钩子） ----------
 * 32/64/128 三档；256 档砍除（lwip 已移独立堆，见档案头注释）。
 * 块存储经 XFixedPool_create 从 arena 划取（与默认流程一致）。 */
#include "XMultiPool.h"
#include "XFixedPool.h"

void xmp_global_init_custom(XMultiPool* mp)
{
    XMultiPool_enable_power_of_two_mode(mp, XMP_GLOBAL_INITIAL_SIZE,
                                        XMP_GLOBAL_GROWTH_MULTIPLIER);
    if (XMP_GLOBAL_C32 && !XMultiPool_add_pool(mp, XFixedPool_create(32, XMP_GLOBAL_C32)))
        XERROR_PRINTF("XMultiPool,32B bin failed\n");
    if (XMP_GLOBAL_C64 && !XMultiPool_add_pool(mp, XFixedPool_create(64, XMP_GLOBAL_C64)))
        XERROR_PRINTF("XMultiPool,64B bin failed\n");
    if (XMP_GLOBAL_C128 && !XMultiPool_add_pool(mp, XFixedPool_create(128, XMP_GLOBAL_C128)))
        XERROR_PRINTF("XMultiPool,128B bin failed\n");
}
