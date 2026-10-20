/**
 * @file gui_main.c
 * @brief ESP8266 + SSD1306 128x64 的 XGui 冒烟固件入口。
 * @details 启动链（照抄 build/arduino_nos 铁律）：eboot -> app_entry
 *          （cre.S 建 cache + 清 .bss，见 stubs.c）-> call_user_start ->
 *          user_pre_init（分区表 + putc1 钩子）-> user_init。
 *          user_init 顺序：
 *            1. UART0 74880 直写 FIFO（带 128B 水位流控）；
 *            2. SSD1306 I2C 初始化 + 清屏（[i2c] 轨迹可审计）；
 *            3. XGui：XGuiApplication -> 顶层窗口（自定义 paintEvent
 *               画 'Hello XinYueC' + 矩形 + 线条）-> show；
 *            4. present 回调接线（XPlatformBackingStore_setPresent_
 *               Callback）-> 合成全窗 EXPOSE -> 逐 tile RGB565->1bpp
 *               -> I2C 上屏；
 *            5. 心跳循环（喂软看门狗 + 堆水位）。
 *          无 FreeRTOS 调度器：全部在 user_init 上下文同步完成。
 */
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "cont.h" /* CONT 上下文（Arduino 同款专用栈） */

/* g_pcont：gui_stubs.c 的 cont 存储（SDK 启动契约 + 本固件 GUI 上下文）。 */
extern cont_t* g_pcont;

#include <string.h>
#include "XClass.h"
#include "XMemory.h"
#include "XTypes.h"
#include "XGeometry.h"
#include "XEvent.h"
#include "XFont.h"
#include "XImage.h"
#include "XPainter.h"
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XWindow.h"
#include "XWindowEvent.h"
#include "XWindowSystemInterface.h"
#include "XBackingStore.h"
#include "XPlatformBackingStore.h"
#include "XGuiApplication.h"
#include "XCoreApplication.h"

/* ---------------- UART0 74880 直写（抄 arduino_nos/app.c） ---------------- */

#define UART0_STATUS (*(volatile unsigned int*)0x6000001c) /* TXFIFO_CNT=bits[23:16] */

static void uart0_wait_tx(void)
{
    while (((UART0_STATUS >> 16) & 0xff) > 120) { }
}

void uart0_puts(const char* s)
{
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    while (*s) {
        uart0_wait_tx();
        *fifo = (unsigned int)(unsigned char)*s++;
    }
}

static void uart0_putc1(char c)
{
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    uart0_wait_tx();
    *fifo = (unsigned int)(unsigned char)c;
}

static void uart0_u(unsigned v)
{
    char b[11];
    int i = 10;
    b[10] = '\0';
    if (!v) {
        uart0_puts("0");
        return;
    }
    while (v && i > 0) {
        b[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    uart0_puts(&b[i]);
}

extern size_t xPortGetFreeHeapSize(void);

static void heap_log(const char* tag)
{
    uart0_puts(tag);
    uart0_u((unsigned)xPortGetFreeHeapSize());
    uart0_puts("\r\n");
}

/* ---------------- SSD1306 驱动（XDeviceDisplay_ssd1306_i2c.c） ---------------- */

extern int XDeviceDisplay_ssd1306_i2c_init(void);
extern void XDeviceDisplay_ssd1306_i2c_clear(void);
extern void XDeviceDisplay_ssd1306_i2c_blit565(int x, int y, int w, int h,
                                               const uint8_t* rgb565,
                                               int strideBytes);
extern void XDeviceDisplay_ssd1306_i2c_flush(void);
extern unsigned XDeviceDisplay_ssd1306_i2c_ackErrors(void);

/* ---------------- XGui present 胶水（RGB565 tile -> 1bpp -> I2C） ---------------- */

#define GUI_W 128
#define GUI_H 64

/* tile 缓冲静态化（档案 XGUI_BACKINGSTORE_BUFFER1 指向此数组）：
 * 128x8 RGB565 = 2048B（单 SSD1306 页高），2 字节对齐。store 创建时经
 * setBuffers 注入，避开 create 时刻堆分配（ESP8266 堆刀刃根因）。 */
unsigned char xguiEsp8266TileBuffer[GUI_W * 8 * 2]
    __attribute__((aligned(4)));

static volatile unsigned g_presentCalls;
static volatile unsigned g_presentRects;

static void board_present(void* userData, XPlatformBackingStore* store,
                          const XRegion* flushedRegion, const XPoint* offset)
{
    XImage* image;
    int ox = offset ? offset->x : 0;
    int oy = offset ? offset->y : 0;
    int i;
    (void)userData;
    ++g_presentCalls;
    image = XPlatformBackingStore_paintDevice(store);
    if (!image || !flushedRegion)
        return;
    {
        uint8_t* bits = XImage_bits(image);
        int stride = XImage_bytesPerLine(image);
        if (!bits || stride <= 0)
            return;
        for (i = 0; i < flushedRegion->count; ++i) {
            const XRect* r = &flushedRegion->rects[i];
            int sx = r->x - ox;
            int sy = r->y - oy;
            if (sx < 0 || sy < 0)
                continue;
            /* PARTIAL 下 tile 源坐标从 (0,0) 起，tile 原点 = offset。 */
            XDeviceDisplay_ssd1306_i2c_blit565(
                r->x, r->y, r->width, r->height,
                bits + (size_t)sy * (size_t)stride + (size_t)sx * 2u,
                stride);
            ++g_presentRects;
        }
    }
    XDeviceDisplay_ssd1306_i2c_flush();
}

/* ---------------- 直绘演示：XPainter 原生 API 逐 band 渲染 + 驱动 flush ----------------
 * 根因（基线串口实证）：XCoreApplication 不建 XGuiApplication 单例 →
 * XGuiApplication_platformNativeInterface() 恒 NULL → XBackingStore 平台
 * 句柄建不起来（基线日志 "present callback wired (global)" 无 +store）→
 * XWidget_paintImage() NULL → paintEvent 空转（presents=0 rects=0，无
 * "[gui] blit tile" 行），像素从未离开过清屏态。
 * 本函数仍走 XGui 原生绘制 API（XPainter_begin_image/drawText/drawRect/
 * drawLine + XFont 内置 8x16 点阵），绘制目标改为档案预留的静态 tile
 * 缓冲（128x8 RGB565 = SSD1306 单页，XImage 外部缓冲包装不拷贝不持有），
 * 逐 band 平移窗口坐标绘制（painter 按 XImage 边界自动裁剪），再经既有
 * 驱动接口 blit565 逐页上屏 + flush——present 回调在本形态下不可达，
 * 上屏走驱动直推（与基线 paintEvent 内直推注释同一结论）。 */
static void gui_demo_draw_flush(void)
{
    XImage tile;
    XPainter painter;
    XFont font;
    XRect rect;
    int band;
    bool textOk = true;
    bool rectOk = true;

    XImage_init(&tile);
    XImage_init_ex_2(&tile, GUI_W, 8, XImageFormat_RGB16,
                     GUI_W * 2, xguiEsp8266TileBuffer, NULL, NULL);
    if (XImage_isNull(&tile)) {
        uart0_puts("[gui] tile image init FAIL\r\n");
        return;
    }
    XFont_init(&font);
    XFont_setPixelSize(&font, 16); /* 8x16 内置点阵，16=原生 1:1 */

    for (band = 0; band < GUI_H / 8; ++band) {
        int bandY = band * 8;
        XPainter_init(&painter, NULL);
        if (!XPainter_begin_image(&painter, &tile)) {
            uart0_puts("[gui] begin_image FAIL\r\n");
            XPainter_deinit(&painter);
            textOk = rectOk = false;
            break;
        }
        /* 平移到 band：后续按窗口坐标绘制，越界像素由 painter 裁剪。 */
        XPainter_translate(&painter, 0.0f, (float)(-bandY));
        XPainter_setFont(&painter, &font);
        /* 白底（OLED 点亮像素，黑字成字）。 */
        XRect_init(&rect, 0, bandY, GUI_W, 8);
        XPainter_fillRect(&painter, &rect, 0xffffffffu);
        /* ①文字 'Hello XinYueC'（XFont 内置 8x16 点阵）。 */
        if (!XPainter_drawText(&painter, 2, 13, "Hello XinYueC", 0xff000000u))
            textOk = false;
        /* ②矩形。 */
        XRect_init(&rect, 2, 22, 124, 20);
        if (!XPainter_drawRect(&painter, &rect))
            rectOk = false;
        /* ③对角线（矩形内左上→右下）。 */
        if (!XPainter_drawLine(&painter, 3, 23, 124, 40))
            rectOk = false;
        XPainter_end(&painter);
        XPainter_deinit(&painter);
        XDeviceDisplay_ssd1306_i2c_blit565(0, bandY, GUI_W, 8,
                                           xguiEsp8266TileBuffer, GUI_W * 2);
    }
    XClassDeinit(&font);
    XClassDeinit(&tile); /* 外部缓冲：仅释放 XImageData 壳，静态数组不动 */
    uart0_puts(textOk ? "[gui] draw text ok\r\n" : "[gui] draw text FAIL\r\n");
    uart0_puts(rectOk ? "[gui] draw rect ok\r\n" : "[gui] draw rect FAIL\r\n");
    XDeviceDisplay_ssd1306_i2c_flush();
    uart0_puts("[gui] flush done\r\n");
}

/* ---------------- 演示窗口：paintEvent 画文字 + 矩形 + 线条 ---------------- */

typedef struct GuiHelloWin {
    XWidget m_base;
} GuiHelloWin;

XCLASS_DEFINE_BEGING(GuiHelloWin)
XCLASS_DEFINE_EXTEND_END(GuiHelloWin, XWidget)

static void GuiHelloWin_paintEvent(XWidget* self, XEvent* event)
{
    XImage* image;
    XPoint offset;
    XPainter painter;
    XRect rect;
    XFont font;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT)
        return;
    uart0_puts("[gui] paintEvent called\r\n");
    image = XWidget_paintImage(self);
    if (!image)
        return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    /* 白底：OLED 点亮像素，黑字/黑线成字（阈值 >=128 点亮）。 */
    XRect_init(&rect, 0, 0, XWidget_width(self), XWidget_height(self));
    XPainter_fillRect(&painter, &rect, 0xffffffffu);
    /* 8x16 内置点阵（XFONT_BUILTIN_8X16_ON），像素尺寸 16 = 原生 1:1。 */
    XFont_init(&font);
    XFont_setPixelSize(&font, 16);
    XPainter_setFont(&painter, &font);
    XClassDeinit(&font);
    XPainter_drawText(&painter, 2, 13, "Hello XinYueC", 0xff000000u);
    XRect_init(&rect, 2, 22, 124, 20);
    XPainter_drawRect(&painter, &rect);
    XPainter_drawLine(&painter, 4, 56, 123, 56);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    /* 直推上屏：库的 PARTIAL present 回调在 ESP8266 上不触发（presents=0
     * ——flush 内部脏区为空）。这里绘完直接把当前 tile（RGB565）按窗口
     * 位置 blit565 到 SSD1306 并 flush，绕开 present 回调，保证自绘
     * 像素确定性上屏。tile 原点=offset，源坐标从 (0,0) 起。 */
    {
        uint8_t* bits = XImage_bits(image);
        int stride = XImage_bytesPerLine(image);
        int tw = XImage_width(image);
        int th = XImage_height(image);
        uart0_puts("[gui] blit tile ");
        uart0_u((unsigned)tw);
        uart0_puts("x");
        uart0_u((unsigned)th);
        uart0_puts(" stride=");
        uart0_u((unsigned)stride);
        uart0_puts(" bits=");
        uart0_u((unsigned)(size_t)bits);
        uart0_puts("\r\n");
        if (bits && stride > 0 && tw > 0 && th > 0) {
            XDeviceDisplay_ssd1306_i2c_blit565(offset.x, offset.y, tw, th,
                                               bits, stride);
            XDeviceDisplay_ssd1306_i2c_flush();
            ++g_presentCalls;
        }
    }
}

static XVtable* GuiHelloWin_class_init(void)
{
    XVTABLE_INIT_DEFAULT(GuiHelloWin)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, GuiHelloWin_paintEvent);
    return XVTABLE_DEFAULT;
}

static GuiHelloWin* gui_hello_win_create(void)
{
    GuiHelloWin* self = (GuiHelloWin*)XMemory_malloc(
        sizeof(GuiHelloWin), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self)
        return NULL;
    memset(self, 0, sizeof(GuiHelloWin));
    XWidget_init(&self->m_base, NULL, 0);
    XClassSetVtable(self, GuiHelloWin);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ---------------- SDK3 分区表（抄 arduino_nos/app.c，4MB 布局） ---------------- */

static const partition_item_t at_partition_table[] = {
    { SYSTEM_PARTITION_PHY_DATA,          0x3FB000, 0x1000 },
    { SYSTEM_PARTITION_RF_CAL,            0x3FC000, 0x1000 },
    { SYSTEM_PARTITION_SYSTEM_PARAMETER,  0x3FD000, 0x3000 },
};

void user_pre_init(void)
{
    os_install_putc1(uart0_putc1);
    if (!system_partition_table_regist(at_partition_table, 3,
                                       system_get_flash_size_map())) {
        uart0_puts("[gui] partition fail\r\n");
        while (1) { }
    }
}

/* ---------------- 主流程 ---------------- */

/* SP 探针：a1=当前栈指针（诊断栈深）。 */
static unsigned gui_sp(void)
{
    register unsigned sp asm("a1");
    return sp;
}

/* GUI 入口：跑在 CONT 上下文（Arduino 同款专用栈）——NONOS 主任务的
 * boot 栈只有 ~2KB 余量，XGuiApplication_create_ex 深调用链直接在
 * user_init 栈上会踩穿（实测 IllegalInstruction epc1=0x76535158）。 */
static void gui_entry(void)
{
    XCoreApplication* app;
    GuiHelloWin* win;
    XBackingStore* bs;
    XPlatformBackingStore* pbs;
    XWindow* wnd;
    XRegion region;
    XRect full;

    uart0_puts("[gui] gui_entry on cont stack, sp=");
    uart0_u(gui_sp());
    uart0_puts("\r\n");
    heap_log("[gui] cont-entry heap free=");

    /* 池已关闭（XMEMORY_MULTIPOOL_ON=0）：XVarList 走 SYSTEM 合并堆，
     * 无需暖池/强制 arena 增长。保留探针打印堆水位。 */
    heap_log("[gui] after pool warmup heap free=");

    /* 诊断：堆可用性 + 类虚表机制 + 应用单例，逐段定位崩溃点。 */
    {
        void* probe = XMemory_malloc(4096, XCLASS_DEFAULT_MEMORY_TYPE);
        uart0_puts("[gui] probe malloc4096 = ");
        uart0_u((unsigned)(size_t)probe);
        uart0_puts("\r\n");
        if (probe)
            XMemory_free(probe, XCLASS_DEFAULT_MEMORY_TYPE);
    }
    uart0_puts("[gui] XGuiApplication_class_init ...\r\n");
    (void)XGuiApplication_class_init();
    uart0_puts("[gui] class_init ok\r\n");
    heap_log("[gui] after class_init heap free=");

    /* 字体探针注：XPainter_textWidth 在 XGuiApplication_create_ex 之前
     * 调用会崩（fontA/B 过、fontC 挂——实测顺序依赖，见遗留问题）；
     * 字体链的验收证据改走 paint 路径（paintEvent 内 drawText，应用
     * 初始化之后），flush 成功即字体成功。 */

    /* 3) 应用单例：用轻量 XCoreApplication（首绘改由直接 flushBacking_
     * Store 驱动，不经 EXPOSE→PAINT 事件链，故不需要 GUI 单例）。 */
    uart0_puts("[gui] xgui init (CoreApplication) ...\r\n");
    {
        static char* argv[] = { (char*)"xgui-ssd1306", NULL };
        app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 1, argv);
    }
    if (!app) {
        uart0_puts("[gui] XCoreApplication create FAILED (OOM?)\r\n");
        while (1) { system_soft_wdt_feed(); }
    }
    uart0_puts("[gui] gui init ok, sp=");
    uart0_u(gui_sp());
    uart0_puts("\r\n");
    heap_log("[gui] after app heap free=");

    /* 4) 顶层窗口（128x64 铺满面板）+ show。 */
    win = gui_hello_win_create();
    if (!win) {
        uart0_puts("[gui] window create FAILED\r\n");
        while (1) { system_soft_wdt_feed(); }
    }
    XWidget_setGeometry(&win->m_base, 0, 0, GUI_W, GUI_H);
    XWidget_showNormal(&win->m_base);
    uart0_puts("[gui] window shown, wiring present ...\r\n");
    heap_log("[gui] after show heap free=");

    /* 5) present 接线：用「全局 present 回调」（任意 store flush 都触发，
     * 无需先拿到平台句柄）——headless 下 backing store 在首次 flush 时
     * 惰性创建，show 后立刻取句柄恒为 NULL（实测根因）。先登记全局回调，
     * 再强制 EXPOSE：首绘 flush 即回调 board_present 上屏。
     * 每存储回调作为补充（若句柄已存在则一并接上）。 */
    XPlatformBackingStore_setGlobalPresentCallback(board_present, NULL);
    bs = XWidget_backingStore(&win->m_base);
    pbs = bs ? XBackingStore_handle(bs) : NULL;
    if (pbs)
        XPlatformBackingStore_setPresentCallback(pbs, board_present, NULL);
    uart0_puts("[gui] present callback wired (global");
    uart0_puts(pbs ? " +store)\r\n" : ")\r\n");

    /* 6) 直接驱动首绘：调 XWidget_flushBackingStore（惰性创建后备存储 →
     * 经 paintEvent 虚表自绘 → 逐 tile flush → 触发 present 回调上屏）。
     * 绕开 EXPOSE→PAINT 事件派发（该链需 XGuiApplication 单例，
     * XCoreApplication 下 paintEvent 永不触发——实测 presents=0 根因）。 */
    wnd = XWidget_windowHandle(&win->m_base);
    if (!wnd) {
        uart0_puts("[gui] window handle NULL\r\n");
        while (1) { system_soft_wdt_feed(); }
    }
    uart0_puts("[gui] direct flushBackingStore -> paint begin\r\n");
    XRect_init(&full, 0, 0, GUI_W, GUI_H);
    /* 先标脏整窗（flush 用累积脏区 m_dirty 决定 present 哪些 tile；
     * 不标脏则脏区为空 → presents=0）。 */
    XWidget_update(&win->m_base);
    XRegion_init(&region);
    XRegion_addRect(&region, &full);
    XWidget_flushBackingStore((XWidget*)&win->m_base, &region);
    XRegion_deinit(&region);
    uart0_puts("[gui] flush 128x64 done presents=");
    uart0_u(g_presentCalls);
    uart0_puts(" rects=");
    uart0_u(g_presentRects);
    uart0_puts(" i2cErrs=");
    uart0_u(XDeviceDisplay_ssd1306_i2c_ackErrors());
    uart0_puts("\r\n");
    heap_log("[gui] after first paint heap free=");

    /* 直绘演示：paintImage 恒 NULL（根因见 gui_demo_draw_flush 头注释），
     * 改由 XPainter 原生 API 逐 band 画进静态 tile 缓冲并驱动上屏。 */
    gui_demo_draw_flush();
    heap_log("[gui] after demo draw heap free=");

    uart0_puts("[gui] init complete\r\n");

    /* 7) 心跳：喂中断看门狗 + 周期堆水位（裸机直入模式没有 SDK 主循环，
     * 喂狗/计时都在本循环内自理；帧已上屏，SSD1306 自持显示）。 */
    {
        extern void gui_wdt_feed(void);
        unsigned long n = 0;
        for (;;) {
            int i;
            gui_wdt_feed();
            for (i = 0; i < 5000000; ++i) /* ~1s 忙等（80MHz 量级） */
                ;
            ++n;
            uart0_puts("[gui] tick#");
            uart0_u((unsigned)n);
            uart0_puts(" heap free=");
            uart0_u((unsigned)xPortGetFreeHeapSize());
            uart0_puts(" i2cErrs=");
            uart0_u(XDeviceDisplay_ssd1306_i2c_ackErrors());
            uart0_puts("\r\n");
        }
    }
}

void user_init(void)
{
    int step = 0;

    uart0_puts("\r\n[gui] uart ok 74880\r\n");
    heap_log("[gui] boot heap free=");

    /* 1) SSD1306 I2C 初始化（[i2c] 轨迹 = START/78W/00 前缀/命令/STOP）。 */
    uart0_puts("[gui] ssd1306 i2c init ...\r\n");
    if (XDeviceDisplay_ssd1306_i2c_init() != 0) {
        uart0_puts("[gui] ssd1306 init FAIL (NACK at init cmd ");
        uart0_u((unsigned)step);
        uart0_puts(")\r\n");
    } else {
        uart0_puts("[gui] ssd1306 init ok\r\n");
    }
    /* 2) 清屏（全页 0 数据事务 + 0xAF 后首帧）。 */
    XDeviceDisplay_ssd1306_i2c_clear();
    uart0_puts("[gui] ssd1306 clear + flush 128x64 done\r\n");
    heap_log("[gui] after lcd heap free=");

    /* 3) XGui 全流程：裸机直入（Cache_Read_Enable 补全 irom 映射后），
     * boot 栈全新（call_user_start 未跑，~8KB 余量），直接在 user_init
     * 栈上跑 gui_entry（绕开 cont）。gui_entry 完成首绘后自跑心跳，
     * 永不返回。 */
    gui_entry();
    /* gui_entry 不应返回；兜底循环（喂狗防复位）。 */
    uart0_puts("[gui] gui_entry returned unexpectedly\r\n");
    for (;;) {
        extern void gui_wdt_feed(void);
        gui_wdt_feed();
    }
}
