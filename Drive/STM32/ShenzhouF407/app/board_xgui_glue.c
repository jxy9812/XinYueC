/* ==================== 神舟号 XGui 平台胶水 ====================
 * 职责（全在 GUI 线程上下文，经 demo 的 postShow 弱钩子挂接）：
 * 1. 上屏：XPlatformBackingStore present 回调 → PARTIAL tile 缓冲逐行
 *    搬到 FSMC GRAM（XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=1，零转换）；
 * 2. 输入：XPT2046 轮询泵挂事件分发器（每轮 processEvents 一次），
 *    单点按下/移动/抬起 → XWindowSystemInterface_handleTouchPoints_ex。
 * 前提：demo 窗口全屏铺满面板（原点 0,0），全局坐标==局部坐标。
 */
#include "XPlatformBackingStore.h"
#include "XBackingStore.h"
#include "XWidget.h"
#include "XWindow.h"
#include "XWindowEvent.h"
#include "XWindowSystemInterface.h"
#include "XGuiApplication.h"
#include "XAbstractEventDispatcher.h"
#include "XTypes.h"
#include "board_lcd.h"
#include "board_touch.h"
#include "board_sys.h"
#include "FreeRTOS.h"
#include "task.h"

/* demo 主文件里定义的弱符号：默认空实现，本文件以强符号覆盖。 */
void xgui_demo_platformPostShow(XWidget* topLevel);

static XWindow* s_window;

/* 观测：present 覆盖范围（gdb 读取）。 */
volatile unsigned long g_presentCount;
volatile unsigned long g_blitPixels;
volatile unsigned long g_blitCalls;
volatile long g_presentMinY = 0x7fffffff;
volatile long g_presentMaxY = -1;
volatile long g_presentMinX = 0x7fffffff;
volatile long g_presentMaxX = -1;
volatile unsigned long g_presentCalls;      /* 入口计数（bail 前递增） */
volatile unsigned long g_presentPaintDevNull;
volatile unsigned long g_postShowRan;
volatile unsigned long g_postShowPbsNull;

/* ------------------------- 上屏回调 ------------------------- */
static void board_present(void* userData, XPlatformBackingStore* store,
                          const XRegion* flushedRegion,
                          const XPoint* offset)
{
    XImage* image;
    int ox = offset ? offset->x : 0;
    int oy = offset ? offset->y : 0;
    int i;
    (void)userData;
    ++g_presentCalls;
    image = XPlatformBackingStore_paintDevice(store);
    if (!image || !flushedRegion)
    {
        ++g_presentPaintDevNull;
        return;
    }
    {
        uint8_t* bits = XImage_bits(image);
        int stride = XImage_bytesPerLine(image);
        if (!bits || stride <= 0)
            return;
        for (i = 0; i < flushedRegion->count; ++i)
        {
            const XRect* r = &flushedRegion->rects[i];
            int sx = r->x - ox;
            int sy = r->y - oy;
            if (sx < 0 || sy < 0)
                continue;
            board_lcd_blit(r->x, r->y, r->width, r->height,
                           bits + (size_t)sy * (size_t)stride
                               + (size_t)sx * 2u,
                           stride);
            ++g_presentCount;
            g_blitPixels += (unsigned long)r->width * (unsigned long)r->height;
            ++g_blitCalls;
            if (r->y < g_presentMinY) g_presentMinY = r->y;
            if (r->y + r->height > g_presentMaxY)
                g_presentMaxY = r->y + r->height;
            if (r->x < g_presentMinX) g_presentMinX = r->x;
            if (r->x + r->width > g_presentMaxX)
                g_presentMaxX = r->x + r->width;
        }
    }
}

/* ------------------------- 触摸轮询泵 ------------------------- */
static bool s_pressed;

/* ------------------------- 兜底重绘轮询 -------------------------
 * 头less 无原生窗口 → 窗口系统不会自发投递 EXPOSE/PAINT 事件，控件树
 * 永远不会首绘。轮询第 30 次时合成一次全窗 EXPOSE（同步分发 → EXPOSE
 * 分支 → 整窗合成提交 → present 回调上屏），此后演示帧泵接管。 */
static XWidget* s_topWidget;
volatile unsigned long g_pollCount;
static bool board_force_paint_poll(void* userData)
{
    (void)userData;
    if ((g_pollCount % 5000) == 100)
    {
        extern size_t xPortGetFreeHeapSize(void);
        extern size_t xPortGetMinimumEverFreeHeapSize(void);
        XPrintf("[heap] free=%u min=%u\n",
                (unsigned)xPortGetFreeHeapSize(),
                (unsigned)xPortGetMinimumEverFreeHeapSize());
    }
    if (s_window && s_topWidget && ++g_pollCount == 30)
    {
        XRegion region;
        XRect full;
        full.x = 0;
        full.y = 0;
        full.width = 320;
        full.height = 480;
        XRegion_init(&region);
        XRegion_addRect(&region, &full);
        (void)XWindowSystemInterface_handleExposeEvent(s_window, &region);
        XRegion_deinit(&region);
    }
    return true;
}

static bool board_touch_poll(void* userData)
{
    int px, py;
    bool down;
    uint32_t ts;
    XTouchPoint point;
    (void)userData;
    if (!s_window)
        return true;
    down = board_touch_read(&px, &py);
    ts = (uint32_t)xTaskGetTickCount();
    if (!down && !s_pressed)
        return true;
    point.m_id = 0;
    point.m_pressure = 1.0f;
    point.m_globalPosition.x = px;
    point.m_globalPosition.y = py;
    point.m_position.x = px;
    point.m_position.y = py;
    if (down && !s_pressed)
    {
        point.m_state = XTOUCHPOINT_STATE_PRESSED;
        if (XWindowSystemInterface_handleTouchPoints_ex(
                s_window, XEVENT_TYPE_TOUCH_BEGIN, &point, 1, ts))
            s_pressed = true;
    }
    else if (down && s_pressed)
    {
        point.m_state = XTOUCHPOINT_STATE_UPDATED;
        (void)XWindowSystemInterface_handleTouchPoints_ex(
            s_window, XEVENT_TYPE_TOUCH_UPDATE, &point, 1, ts);
    }
    else /* (!down && s_pressed) */
    {
        point.m_state = XTOUCHPOINT_STATE_RELEASED;
        (void)XWindowSystemInterface_handleTouchPoints_ex(
            s_window, XEVENT_TYPE_TOUCH_END, &point, 1, ts);
        s_pressed = false;
    }
    return true;
}

/* ------------------------- postShow 弱钩子覆盖 -------------------------
 * demo 在窗口 show 之后、事件循环之前调用（单点接线位）。 */
void xgui_demo_platformPostShow(XWidget* topLevel)
{
    XBackingStore* bs;
    XPlatformBackingStore* pbs;
    if (!topLevel)
        return;
    s_topWidget = topLevel;
    s_window = XWidget_windowHandle(topLevel);
    bs = XWidget_backingStore(topLevel);
    pbs = bs ? XBackingStore_handle(bs) : NULL;
    {
        extern size_t xPortGetFreeHeapSize(void);
        XPrintf("[mem] postShow: free=%u\n",
                (unsigned)xPortGetFreeHeapSize());
    }
    g_postShowRan = 1;
    g_postShowPbsNull = pbs ? 0 : 1;
    if (pbs)
        XPlatformBackingStore_setPresentCallback(pbs, board_present, NULL);
    (void)XAbstractEventDispatcher_addPollCallback(board_touch_poll, NULL);
    (void)XAbstractEventDispatcher_addPollCallback(board_force_paint_poll,
                                                   NULL);
}
