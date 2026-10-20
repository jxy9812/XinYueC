/* GUI 版链接存根 = arduino_nos/stubs.c（启动铁律：cre.S 建 cache + 清 .bss
 * + 真实堆 pvPort* + wrap spi_flash）+ esp8266_console/console_stubs.c 的
 * FreeRTOS/原子/断言胶水（FreeRTOS 内核对象随 XSync 拉入但调度器不启动，
 * 队列/互斥在单线程不阻塞路径下可用）。
 * 堆：[_heap_start, 0x3FFFC000) 首次适应 + 合并释放（不引 FreeRTOS heap_4，
 * 省 24~32KB 静态 ucHeap；GUI 与 SDK 共享同一堆）。 */
extern void call_cache_enable_dout(void);
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>     /* memset/memcpy */
#include <sys/time.h>   /* settimeofday 原型 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "user_interface.h"

void call_user_start(void);
/* 启动链（arduino_nos 铁律，SDK 完整启动定版）：eboot → app_entry
 * （cre.S 建 cache + 清 .bss）→ call_user_start（NONOS3 SDK init：装
 * 异常向量/开 irom 取指/建任务栈 → user_pre_init → user_init）。
 * 实测结论：(a) 裸机直入（跳过 call_user_start）下 cont_run/深调用
 * 会 irom 取指异常（SDK 的 cache/异常向量初始化缺失）；(b) 完整启动
 * 链 + cont 4KB 是唯一跑到 "xgui init" 的组合——后续由 pool 收缩+
 * 暖池解 OOM。gui_wdt_feed 备用（libpp 已提供 system_soft_wdt_feed，
 * 此处仅 gui_main 心跳兜底用）。 */
void gui_wdt_feed(void);
void gui_wdt_feed(void) {
    *(volatile unsigned char*)0x60000600 = 0;
    *(volatile unsigned int*)0x60000914 = 115;
}
/* 裸机直入（无 libpp）：喂狗走 gui_wdt_feed 寄存器直写。 */
void system_soft_wdt_feed(void) { gui_wdt_feed(); }
void system_soft_wdt_stop(void) { }
void system_soft_wdt_restart(void) { }

void app_entry(void) __attribute__((noreturn));
void app_entry(void) {
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    *fifo = 'E';   /* eboot 跳转成功，进入应用入口 */
    call_cache_enable_dout();  /* SPI0 寄存器直接配置 cache（dout 模式） */
    /* eboot 不清 .bss —— SDK 全局变量会是 RAM 垃圾，这里自己清零 */
    {
        extern char _bss_start[], _bss_end[];
        volatile char* p = _bss_start;
        while (p < _bss_end) *p++ = 0;
    }
    *fifo = 'X';   /* cache enable 返回 */
    /* 裸机直入：直进 user_init（call_user_start 的 SDK 启动吃掉 boot 栈
     * 到 ~2KB 且触发 multipool 碎片 OOM）。Cache_Read_Enable(0,0,1) 补
     * 全 irom 页映射（cre.S 仅手工配 SPI0，深调用跨 32KB MMU 页边界时
     * 取指异常——裸机态 cont_run/深链崩溃根因假说）。 */
    {
        void Cache_Read_Enable(unsigned, unsigned, unsigned);
        Cache_Read_Enable(0, 0, 1);
    }
    {
        extern void user_init(void);
        user_init();
    }
    *fifo = 'R';   /* user_init 返回（不该发生） */
    for(;;){ gui_wdt_feed(); }
}

/* ---------- 真实 heap：[_heap_start, 0x3FFFC000)，同 Arduino umm 边界 ---------- */
extern char _heap_start[];
#define HEAP_END_ADDR 0x3FFFC000UL

/* ets_intr_lock/unlock：NONOS3 头声明为 void（ROM --defsym 钉桩）。 */

typedef struct blk_s {
    struct blk_s* next;    /* 地址序：空闲与已用块同链 */
    struct blk_s* prev;
    unsigned int  size;    /* 载荷字节数（8 对齐） */
    unsigned int  used;
} blk_t;

static blk_t* h_head = 0;
static unsigned int h_free = 0;
static unsigned int h_inited = 0;

static void h_init(void) {
    unsigned int start = ((unsigned int)(size_t)_heap_start + 7u) & ~7u;
    unsigned int end   = (unsigned int)HEAP_END_ADDR;
    h_inited = 1;
    if (end < start + sizeof(blk_t) + 32u) return;
    h_head = (blk_t*)start;
    h_head->prev = 0;
    h_head->next = 0;
    h_head->size = end - start - sizeof(blk_t);
    h_head->used = 0;
    h_free = h_head->size;
}

static void* h_alloc(unsigned int n) {
    blk_t* p;
    if (!h_inited) h_init();
    n = (n + 7u) & ~7u;
    if (n == 0) n = 8;
    for (p = h_head; p; p = p->next) {
        if (!p->used && p->size >= n) {
            if (p->size >= n + sizeof(blk_t) + 8u) {
                blk_t* q = (blk_t*)((char*)(p + 1) + n);   /* split */
                q->next = p->next;
                q->prev = p;
                if (q->next) q->next->prev = q;
                p->next = q;
                q->size = p->size - n - sizeof(blk_t);
                q->used = 0;
                p->size = n;
                h_free -= sizeof(blk_t);
            }
            p->used = 1;
            h_free -= p->size;
            return (void*)(p + 1);
        }
    }
    return 0;
}

static void h_release(void* v) {
    blk_t* p;
    unsigned int a;
    unsigned int lo = ((unsigned int)(size_t)_heap_start + 7u) & ~7u;
    if (!v) return;
    a = (unsigned int)(size_t)v;
    if (a < lo + sizeof(blk_t) || a >= (unsigned int)HEAP_END_ADDR) return;
    p = ((blk_t*)v) - 1;
    if (!p->used) return;                      /* 双重释放保护 */
    p->used = 0;
    h_free += p->size;
    if (p->next && !p->next->used) {           /* 并入后邻 */
        blk_t* q = p->next;
        p->size += sizeof(blk_t) + q->size;
        p->next = q->next;
        if (p->next) p->next->prev = p;
        h_free += sizeof(blk_t);
    }
    if (p->prev && !p->prev->used) {           /* 并入前邻 */
        blk_t* r = p->prev;
        r->size += sizeof(blk_t) + p->size;
        r->next = p->next;
        if (p->next) p->next->prev = r;
        h_free += sizeof(blk_t);
    }
}

void* pvPortMalloc(size_t n) {
    void* p;
    ets_intr_lock();
    p = h_alloc((unsigned int)n);
    ets_intr_unlock();
    if (!p) {
        /* OOM 诊断：直接串口打点（heap 10KB 级目标上 GUI init 的
         * 关键观测；uart0_puts 在 gui_main.c）。 */
        extern void uart0_puts(const char* s);
        char b[32];
        unsigned v = (unsigned)n;
        int i = 20;
        b[20] = '\0';
        if (!v) { b[--i] = '0'; }
        while (v && i > 0) { b[--i] = (char)('0' + (v % 10)); v /= 10; }
        b[--i] = '='; b[--i] = 'n'; b[--i] = ' '; b[--i] = 'M';
        b[--i] = 'O'; b[--i] = 'O'; b[--i] = 'm'; b[--i] = ' ';
        b[--i] = ']'; b[--i] = 'm'; b[--i] = 'e'; b[--i] = 'm';
        b[--i] = '[';
        uart0_puts(&b[i]);
    }
    return p;
}
void* pvPortZalloc(size_t n) {
    void* p;
    ets_intr_lock();
    p = h_alloc((unsigned int)n);
    ets_intr_unlock();
    if (p) {
        unsigned char* b = (unsigned char*)p;
        size_t i;
        for (i = 0; i < n; i++) b[i] = 0;
    }
    return p;
}
void vPortFree(void* p) {
    ets_intr_lock();
    h_release(p);
    ets_intr_unlock();
}
void* sdk3_pvPortMalloc(size_t n) { return pvPortMalloc(n); }
void* pvPortMalloc_(size_t n) { return pvPortMalloc(n); }
void vPortFree_(void* p) { vPortFree(p); }
unsigned int xPortGetFreeHeapSize(void) { return h_free; }
unsigned int xPortGetMinimumEverFreeHeapSize(void) { return h_free; }
unsigned int xPortWantedSizeAlign(unsigned int n) { return (n + 7u) & ~7u; }

/* newlib malloc → 同一 heap（原型对齐 newlib reent 族）。 */
struct _reent;
void* _malloc_r(struct _reent* r, size_t n) { (void)r; return pvPortMalloc(n); }
void _free_r(struct _reent* r, void* p) { (void)r; vPortFree(p); }
void* _realloc_r(struct _reent* r, void* p, size_t n) {
    void* q;
    unsigned int i, oldn;
    (void)r;
    if (!p) return pvPortMalloc(n);
    q = pvPortMalloc(n);
    if (!q) return 0;
    /* 旧大小在块头里（我们自己的分配器） */
    oldn = ((blk_t*)p - 1)->size;
    if (oldn > n) oldn = (unsigned int)n;
    for (i = 0; i < oldn; i++) ((unsigned char*)q)[i] = ((unsigned char*)p)[i];
    vPortFree(p);
    return q;
}
void* _calloc_r(struct _reent* r, size_t c, size_t n) { void* p = pvPortZalloc(c*n); (void)r; return p; }
void* malloc(unsigned int n) { return pvPortMalloc(n); }
void free(void* p) { vPortFree(p); }
void* realloc(void* p, unsigned int n) { return _realloc_r(0, p, n); }
void* calloc(unsigned int c, unsigned int n) { return pvPortZalloc(c*n); }

/* newlib 系统调用存根（stdout 走 UART0 带水位流控；其余丢弃/空操作）。 */
int _read_r(void* r, int fd, char* buf, int len) { (void)r;(void)fd;(void)buf;(void)len; return 0; }
int _write_r(void* r, int fd, const char* buf, int len) {
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    int i;
    (void)r;
    if (fd > 2) return -1;
    for (i = 0; i < len; i++) {
        while ((((*(volatile unsigned int*)0x6000001c) >> 16) & 0xff) > 120) { }
        *fifo = (unsigned char)buf[i];
    }
    return len;
}
int _lseek_r(void* r, int fd, int off, int whence) { (void)r;(void)fd;(void)off;(void)whence; return 0; }
int _close_r(void* r, int fd) { (void)r;(void)fd; return 0; }
int _fstat_r(void* r, int fd, void* st) { (void)r;(void)fd;(void)st; return 0; }
int _isatty_r(void* r, int fd) { (void)r; return fd>=0 && fd<=2; }
void* _sbrk_r(void* r, int incr) { (void)r;(void)incr; return (void*)-1; }

/* lwip 空存根（WiFi 基础 bring-up 不接真实 lwip；DHCP server 未启用）。
 * 注意：lwip2 arch/cc.h（经 user_interface.h 链引入）把下面这批符号
 * #define 成 *_LWIP2 变体——NONOS 库组要的是 plain 名，定义前全部
 * #undef 还原；dhcps_start_LWIP2 另行提供（liblwip2 glue 引用）。 */
#undef dhcp_cleanup
#undef dhcp_release
#undef dhcp_start
#undef dhcp_stop
#undef dhcps_start
#undef espconn_init
#undef etharp_output
#undef ethbroadcast
#undef ethernet_input
#undef lwip_init
#undef netif_add
#undef netif_default
#undef netif_remove
#undef netif_set_addr
#undef netif_set_default
#undef netif_set_down
#undef netif_set_up
#undef pbuf_alloc
#undef pbuf_free
#undef pbuf_ref
struct netif; struct pbuf; struct ip_addr;
int dhcp_start(void* n){(void)n;return 0;}
void dhcp_stop(void* n){(void)n;}
void dhcp_cleanup(void* n){(void)n;}
int dhcp_release(void* n,void*a){(void)n;(void)a;return 0;}
int dhcps_start(void* n){(void)n;return 0;}
void dhcps_stop(void* info){(void)info;}
/* liblwip2 的 glue-lwip 引用（lwip-git.c:454/499），本固件不起 SoftAP。 */
void dhcps_start_LWIP2(void* info){(void)info;}
int espconn_init(void){return 0;}
int etharp_output(void*a,void*b){(void)a;(void)b;return 0;}
char ethbroadcast[6]={0xff,0xff,0xff,0xff,0xff,0xff};
int ethernet_input(void*a,void*b){(void)a;(void)b;return 0;}
void lwip_init(void){}
/* netif_add/netif_set_addr：不能纯空实现 —— wifi_get_ip_info(SOFTAP_IF) 读的是
 * netif 结构偏移 4/8/12（ip/netmask/gw，lwip 布局），这里把 IP 真正抄进去，
 * 否则拿到的是 netif 内存里的垃圾（实测读到 "if%d" 格式串残留）。 */
void* netif_add(void*n,void*ip,void*mask,void*gw,void*e,void*f,void*g){
    unsigned* w=(unsigned*)n;
    (void)e;(void)f;(void)g;
    if(n){
        if(ip)w[1]=*(unsigned*)ip;
        if(mask)w[2]=*(unsigned*)mask;
        if(gw)w[3]=*(unsigned*)gw;
    }
    return n;
}
void* netif_default=0;
void netif_remove(void*a){(void)a;}
void netif_set_addr(void*n,void*ip,void*mask,void*gw){
    unsigned* w=(unsigned*)n;
    if(!n)return;
    if(ip)w[1]=*(unsigned*)ip;
    if(mask)w[2]=*(unsigned*)mask;
    if(gw)w[3]=*(unsigned*)gw;
}
void netif_set_default(void*a){(void)a;}
void netif_set_down(void*a){(void)a;}
void netif_set_up(void*a){(void)a;}
void* pbuf_alloc(void*a,unsigned short b,char c){(void)a;(void)b;(void)c;return 0;}
int pbuf_copy(void*a,void*b){(void)a;(void)b;return 0;}
void pbuf_free(void*a){(void)a;}
void pbuf_ref(void*a){(void)a;}
void sys_check_timeouts(void){}

/* ---- XMultiPool 档（XMEMORY_MULTIPOOL_ON=0）：XMultiPool.c 空转，
 * XVarList.c:36 无守卫调用 XMultiPool_global() —— 打桩返回 NULL，
 * XVarList 走 SYSTEM 槽（pvPortZalloc 合并堆）。global_malloc/free 仅
 * 链接占位（global()=NULL 时不可达）。 ---- */
struct XMultiPool;
struct XMultiPool* XMultiPool_global(void) { return NULL; }
void* XMultiPool_global_malloc(unsigned n) { (void)n; return NULL; }
void XMultiPool_global_free(void* p) { (void)p; }
void* XMultiPool_global_realloc(void* p, unsigned n) { (void)p; (void)n; return NULL; }
void* XMultiPool_global_calloc(unsigned c, unsigned n) { (void)c; (void)n; return NULL; }
int XMultiPool_global_isInited(void) { return 0; }
int XMultiPool_is_from_pool(struct XMultiPool* p, void* ptr) { (void)p; (void)ptr; return 0; }
unsigned long XMultiPool_totalSize(struct XMultiPool* p) { (void)p; return 0; }

/* ---- newlib 杂项：_exit/atexit（abort/exit 路径引用） ---- */
void _exit(int code)
{
    (void)code;
    system_restart();
    for (;;) { }
}
int atexit(void (*fn)(void))
{
    (void)fn;
    return 0;    /* 无进程退出语义，登记无处触发 */
}

/* ---- 面板铺底/快照直写：unsupported 后端无面板，no-op（STM32
 * board_stubs.c 同款存根；本固件窗口=整屏，不需要面板带） ---- */struct XRect; struct XImage; struct XPoint; struct XWindow;
void XPlatformBackingStore_fillPanelRects(const struct XRect* rects, int count,
                                          uint32_t nativePixel)
{
    (void)rects; (void)count; (void)nativePixel;
}
bool XPlatformBackingStore_blitSnapshotPanelRects(const struct XImage* snapshot,
                                                  const struct XRect* rects,
                                                  int count,
                                                  const struct XPoint* origin,
                                                  const struct XWindow* selfWindow)
{
    (void)snapshot; (void)rects; (void)count; (void)origin; (void)selfWindow;
    return false;
}

/* ---- 文件设备平台层：无文件系统，stat/权限一律拒绝（STM32 board_stubs
 * 同款；XFile 对象层经 XFile.c 引用，XIMAGECODEC 关闭后仅剩空路径）。 ---- */
#include "XDeviceFile.h"
bool XDeviceFile_stat(const XString* path, XFileStat* stat)
{
    (void)path; (void)stat;
    return false;
}
bool XDeviceFile_setPermissions(const XString* path,
                                XFilePermissions permissions)
{
    (void)path; (void)permissions;
    return false;
}

/* ---- 图像插件注册表存根：XIMAGEIOPLUGIN_ON=0 时注册表 TU 裁空，但
 * XIcon/XImageReader（虚表可达）无守卫引用 7 个入口——这里给"无插件"
 * 等价实现（无文件系统，图标/图片文件加载路径运行期不可达）。 ---- */
#include "XImageIOHandler.h"
#include "XImagePluginRegistry.h"
XImageIOHandler* XImagePluginRegistry_createReadHandler(XIODevice* device,
                                                        const XString* format)
{
    (void)device; (void)format;
    return NULL;
}
XImageIOHandler* XImagePluginRegistry_createReadHandlerEx(
    XIODevice* device, const XString* format,
    bool autoDetectImageFormat, bool decideFormatFromContent)
{
    (void)device; (void)format;
    (void)autoDetectImageFormat; (void)decideFormatFromContent;
    return NULL;
}
XImageIOHandler* XImagePluginRegistry_createReadHandlerSuffix(
    XIODevice* device, const XString* suffixFormat)
{
    (void)device; (void)suffixFormat;
    return NULL;
}
XImageIOHandler* XImagePluginRegistry_createReadHandlerContentFallback(
    XIODevice* device, const XString* rejectedFormat)
{
    (void)device; (void)rejectedFormat;
    return NULL;
}
bool XImagePluginRegistry_supportsReadFormat(const XString* format)
{
    (void)format;
    return false;
}
XStringList* XImagePluginRegistry_supportedImageFormats(bool readOnly)
{
    (void)readOnly;
    return NULL;
}
XStringList* XImagePluginRegistry_supportedMimeTypes(bool readOnly)
{
    (void)readOnly;
    return NULL;
}
XStringList* XImagePluginRegistry_imageFormatsForMimeType(
    const XString* mimeType, bool readOnly)
{
    (void)mimeType; (void)readOnly;
    return NULL;
}

/* Arduino core 的 cont 上下文：裸机直入模式不使用 cont（user_init 直接
 * 跑 gui_entry，boot 栈 ~8KB 足够）——回收 4KB 静态。g_pcont 保留 NULL
 * 定义（cont.h 声明 extern，无人解引用）。 */
#include "cont.h"
cont_t* g_pcont = NULL;

/* wrap spi_flash_read：
 * 直接调 __real_spi_flash_read（Disable_2 → SPIRead → Enable_2，
 * 物理直读永远最新）；'<'/'>' 保留作挂死定位。 */
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

/* wrap 写/擦：只打标记不改行为（诊断 syscfg "sec 3fd error" 死循环）
 * e=erase 开始 E=erase 成功 o=erase 失败
 * w=write 开始 W=write 成功 x=write 失败 */
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

/* ============ console_stubs.c 的 FreeRTOS/原子/断言胶水 ============ */

/* __atomic_*_8：LX106 无 8 字节原子指令，GCC 降级 libcall，工具链无
 * libatomic——用 FreeRTOS 关中断构成临界区补齐（XAtomicLibcall_esp8266.c
 * 只实现了 1/4 字节宽）。 */

uint64_t __atomic_load_8(volatile void* ptr, int memorder)
{
    uint64_t result;
    (void)memorder;
    taskENTER_CRITICAL();
    result = *(volatile const uint64_t*)ptr;
    taskEXIT_CRITICAL();
    return result;
}

void __atomic_store_8(volatile void* ptr, uint64_t value, int memorder)
{
    (void)memorder;
    taskENTER_CRITICAL();
    *(volatile uint64_t*)ptr = value;
    taskEXIT_CRITICAL();
}

uint64_t __atomic_exchange_8(volatile void* ptr, uint64_t value, int memorder)
{
    uint64_t result;
    (void)memorder;
    taskENTER_CRITICAL();
    result = *(volatile uint64_t*)ptr;
    *(volatile uint64_t*)ptr = value;
    taskEXIT_CRITICAL();
    return result;
}

_Bool __atomic_compare_exchange_8(volatile void* ptr, void* expected,
                                  uint64_t desired, int weak, int succ, int fail)
{
    _Bool result;
    (void)weak; (void)succ; (void)fail;
    taskENTER_CRITICAL();
    if (*(volatile uint64_t*)ptr == *(volatile const uint64_t*)expected) {
        *(volatile uint64_t*)ptr = desired;
        result = 1;
    }
    else {
        *(uint64_t*)expected = *(volatile const uint64_t*)ptr;   /* 回写当前值 */
        result = 0;
    }
    taskEXIT_CRITICAL();
    return result;
}

uint64_t __atomic_fetch_add_8(volatile void* ptr, uint64_t value, int memorder)
{
    uint64_t result;
    (void)memorder;
    taskENTER_CRITICAL();
    result = *(volatile uint64_t*)ptr;
    *(volatile uint64_t*)ptr = result + value;
    taskEXIT_CRITICAL();
    return result;
}

uint64_t __atomic_fetch_sub_8(volatile void* ptr, uint64_t value, int memorder)
{
    uint64_t result;
    (void)memorder;
    taskENTER_CRITICAL();
    result = *(volatile uint64_t*)ptr;
    *(volatile uint64_t*)ptr = result - value;
    taskEXIT_CRITICAL();
    return result;
}

/* FreeRTOS 钩子/断言（FreeRTOSConfig.h:84 契约：应用侧实现）。断言失败
 * 先打点再软复位——比死循环看门狗更可恢复。 */
void vAssertCalled(const char* pcFileName, unsigned long ulLine)
{
    (void)pcFileName; (void)ulLine;
    {
        volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
        *fifo = 'A'; *fifo = '!';
    }
    system_restart();
}

void vApplicationMallocFailedHook(void)
{
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    *fifo = 'M'; *fifo = '!';
    system_restart();
}

void vApplicationStackOverflowHook(xTaskHandle xTask, char* pcTaskName)
{
    (void)xTask; (void)pcTaskName;
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    *fifo = 'S'; *fifo = '!';
    system_restart();
}

/* heap_4.c:328 的失败诊断全局（本固件无 heap_4，保留供链接兜底）。 */
volatile unsigned long g_lastMallocFailSize = 0;

/* 链接后 symtab 里可能残留的悬空 UND（来源不可考）：弱定义清账。 */
__attribute__((weak)) const char core_version[] = "00000000";

/* XTENSA_LX106 port.c 的中断胶水（NONOS3 无 RTOS SDK 的 context 汇编；
 * 本固件不启动调度器，这些符号只需链接存在）。 */
char NMIIrqIsOn = 0;    /* 单核固件不启用 NMI，恒 0 → 关中断路径全程生效 */

portBASE_TYPE MacIsrSigPostDefHdl(void)
{
    return pdFALSE;
}

void _xt_user_exit(void)
{
    /* 仅当任务入口函数 return 时才会执行到：等价于任务自删。 */
    vTaskDelete(NULL);
    for (;;) { }
}

/* millis（arduino cores 同名符号）：不能用 portTICK_RATE_MS（XTENSA_LX106/
 * portmacro.h 旧名被 FreeRTOS.h 重定义为缺失的 portTICK_PERIOD_MS）。 */
unsigned long millis(void)
{
    return (unsigned long)xTaskGetTickCount * (1000ul / configTICK_RATE_HZ);
}

/* ---- GPU 驱动存根：ESP8266 无 GL——XGpuRenderBackend 工厂取 NULL 表，
 * 有序回退（vulkan -> gl -> software）落软件路径，与 --software 桌面
 * 口径同构。XGPU_ON=1 仅为满足 XPainter.c 无守卫成员引用（编译期）。 ---- */
#include "XGpuRenderDriver.h"
const XGpuRenderDriverProcs* XGpuRenderDriver_gl_procs(void)
{
    return NULL;
}
bool XGpuRenderDriver_gl_readbackRect(XGpuRenderDriverSession* self,
                                      XImage* target, int x, int y,
                                      int width, int height)
{
    (void)self; (void)target; (void)x; (void)y; (void)width; (void)height;
    return false;
}

int settimeofday(const struct timeval* tv, const struct timezone* tz)
{
    /* 无 RTC：对时无处可写，按成功返回以免调用方（lwip glue）走错误分支。 */
    (void)tv; (void)tz;
    return 0;
}
