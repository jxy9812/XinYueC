/******************************************************************************
 * @file       xgui_window_demo.c
 * @brief      XGui GUI 控件统一可视化测试程序（Linux X11 / Windows Win32）。
 * @details    本程序是 GUI 控件的人工可视化验收入口，演示 XGui 完整窗口链路：
 *             - XGuiApplication_create_ex 初始化应用单例；
 *             - 顶层 XWidget 的 showNormal() 惰性创建内部 XWidgetWindow，
 *               再由该桥接窗口创建平台窗口（X11 Window / Win32 HWND）；
 *             - XWidget 首次绘制时创建离屏缓冲，并由 XPainter 软件光栅化；
 *             - XWindowSystemInterface 事件注入（Expose/Resize/Close）；
 *             - XGuiApplication_exec 标准事件循环与对象定时器刷新；
 *             - WM 删除（标题栏 X）触发 CloseEvent -> 接受后关闭退出。
 *             窗口绘制完全走 XImage/XPainter 软件路径，不依赖任何平台
 *             图形 API；平台差异全部隔离在 Drive 后端。
 *             新增控件时在本文件追加可见场景；自动断言仍统一放在
 *             xgui_regression_test.c，避免菜单式测试程序重复。
 *             运行：./bin/XGuiWindowDemo_Test [自动退出秒数]
 *                   - 不带参数：常驻，等待窗口标题栏关闭；
 *                   - 带参数：运行指定秒数后自动退出（供无窗口管理器的
 *                     Xvfb/CI 环境截图验收）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "CXinYueConfig.h"
#include "XPrintf.h"
#include "XObject.h"
#include "XEvent.h"
#include "XDateTime.h"
#include "XGuiApplication.h"
#include "XWidget.h"
#include "XWindow.h"
#include "XWindowEvent.h"
#include "XImage.h"
#include "XPainter.h"
#include "XLabel.h"
#include "XNetworkStats.h"
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
#include "XPerformanceOverlay.h"
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON
#include "XPushButton.h"
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
#include "XStackedLayout.h"
#endif

#if XGUIAPPLICATION_ON && XWIDGET_ON && XWINDOW_ON && XBACKINGSTORE_ON && \
    XPLATFORMINTEGRATION_ON && XPLATFORMNATIVEWINDOW_ON

/* The demo follows the scalable built-in face whenever it is compiled in.
   A clipped embedded build keeps the existing bitmap fallback. */
#if XFONT_BUILTIN_OUTLINE_ON
#define XGUI_DEMO_DEFAULT_FONT_FAMILY "XFontOutlineCommon"
#else
#define XGUI_DEMO_DEFAULT_FONT_FAMILY XFONT_DEFAULT_FAMILY
#endif

/* Desktop demo 默认缓存静态控件场景；资源受限目标可显式设为 0。 */
#ifndef XGUI_DEMO_STATIC_SCENE_CACHE_ON
#define XGUI_DEMO_STATIC_SCENE_CACHE_ON 1
#endif

/* 由标准事件循环中的精确定时器驱动性能采样和画面提交。 */
#ifndef XGUI_DEMO_FRAME_INTERVAL_MS
#define XGUI_DEMO_FRAME_INTERVAL_MS 1
#endif

/* ==================== 演示窗口子类 ==================== */

XCLASS_DEFINE_BEGING(DemoWin)
XCLASS_DEFINE_EXTEND_END(DemoWin, XWidget)

/** @brief 演示顶层控件：XWidget 负责窗口桥接、后备存储和控件树。 */
typedef struct DemoWin
{
    XWidget         m_base;  /**< 顶层控件基类；必须是第一个成员。 */
    XImage          m_staticScene; /**< 不含性能悬浮层的静态场景缓存。 */
    bool            m_staticSceneDirty; /**< 静态场景需重新生成。 */
    XTimerId        m_frameTimer; /**< 性能采样与刷新定时器。 */
    XTimerId        m_autoQuitTimer; /**< 自动退出定时器。 */
    bool            m_closed; /**< CloseEvent 被接受或自动退出后置真。 */
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XPerformanceOverlay m_performanceOverlay; /**< 性能悬浮层控件。 */
#if XGUI_PERFORMANCE_OVERLAY_NETWORK_ON
    int64_t m_lastNetworkPollUsecs; /**< 最近一次主机网络计数采样时刻。 */
#endif
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON
    XPushButton     m_button; /**< 常驻按钮控件：输入与信号都走控件自身。 */
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel          m_linkLabel; /**< 常驻联动标签：按下/松开文本由按钮信号槽更新。 */
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    XStackedLayout  m_stackLayout; /**< 堆叠页面布局：展示 StackOne 页面切换策略。 */
    XLabel          m_stackPageOne; /**< 堆叠布局第一个页面控件。 */
    XLabel          m_stackPageTwo; /**< 堆叠布局第二个页面控件。 */
#if XPUSHBUTTON_ON
    XPushButton     m_stackPrevButton; /**< 堆叠布局上一页按钮。 */
    XPushButton     m_stackNextButton; /**< 堆叠布局下一页按钮。 */
#endif
#endif
} DemoWin;

#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
static int64_t demo_monotonicUsecs(void);
#endif

/* ---------------- 绘制 ----------------
 * 全部使用 ARGB32 预乘颜色：软件光栅化由 XWidget 后备存储统一上屏，
 * 与平台（X11/Win32）无关。 */

/** @brief 填充一个矩形（坐标自动裁剪到窗口内）。 */
static void demo_fill_rect(XPainter* painter, int x, int y, int w, int h,
                           uint32_t argb)
{
    XRect rect;
    if (w <= 0 || h <= 0) return;
    XRect_init(&rect, x, y, w, h);
    XPainter_fillRect(painter, &rect, argb);
}

/** @brief 将 demo 使用的 XFont 默认家族设置为当前可用的内置字库。 */
static void demo_apply_default_font(XFont* font)
{
    if (font)
        XFont_setFamily(font, XGUI_DEMO_DEFAULT_FONT_FAMILY);
}

#if XWIDGET_ON
/** @brief 将 demo 默认字体应用到一个控件，供控件内部文字绘制使用。 */
static void demo_set_widget_default_font(XWidget* widget)
{
    XFont font;
    if (!widget)
        return;
    XFont_init(&font);
    demo_apply_default_font(&font);
    XWidget_setFont(widget, &font);
    XFont_deinit_base(&font);
}
#endif /* XWIDGET_ON */

/** @brief 画棋盘格纹理：验证亚像素级脏区提交正确性。 */
static void demo_draw_checker(XPainter* painter, int x0, int y0,
                              int cols, int rows, int cell)
{
    int xi;
    int yi;
    for (yi = 0; yi < rows; ++yi) {
        for (xi = 0; xi < cols; ++xi) {
            /* 棋盘奇偶格交替填色，保证每个 8x8 像素块都可区分。 */
            if (((xi + yi) & 1) != 0)
                demo_fill_rect(painter, x0 + xi * cell, y0 + yi * cell,
                               cell, cell, 0xffc8d8e8u);
            else
                demo_fill_rect(painter, x0 + xi * cell, y0 + yi * cell,
                               cell, cell, 0xff4a7ba6u);
        }
    }
}

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
/**
 * @brief 在窗口后备缓冲中绘制一个可见的 XLabel 测试场景。
 * @param painter 已绑定后备缓冲的绘制器。
 * @param x 标签在窗口客户区中的横坐标。
 * @param y 标签在窗口客户区中的纵坐标。
 * @param width 标签可用宽度。
 * @param height 标签可用高度。
 * @param text 要显示的 UTF-8 文本。
 * @param pixelSize 标签文字像素高度，16 为原始点阵字号，32 为两倍放大。
 * @param family 字库 family；NULL 使用 demo 当前默认字库。
 */
static void demo_draw_label(XPainter* painter, int x, int y, int width,
                            int height, const char* text, int pixelSize,
                            const char* family)
{
    XLabel label;
    if (!painter || width <= 0 || height <= 0) return;
    memset(&label, 0, sizeof(label));
    XLabel_init(&label, NULL, 0);
    {
        XFont labelFont = XWidget_font((XWidget*)&label);
        XFont_setFamily(&labelFont,
                        family ? family : XGUI_DEMO_DEFAULT_FONT_FAMILY);
        XWidget_setFont((XWidget*)&label, &labelFont);
        XFont_deinit_base(&labelFont);
    }
    XLabel_setText_2(&label, text);
    XLabel_setTextPixelSize(&label, pixelSize);
    XLabel_setAlignment(&label, XAlignment_Left | XAlignment_Top);
    XWidget_resize((XWidget*)&label, width, height);
    if (XPainter_save(painter)) {
        XPainter_translate(painter, (float)x, (float)y);
        XLabel_drawContents(&label, painter);
        XPainter_restore(painter);
    }
    XLabel_deinit_base(&label);
}
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

/* ==================== 性能悬浮层 ==================== */

#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON

/** @brief 初始化性能悬浮层；位置由控件保留，demo 在后备存储上叠加绘制。 */
static void demo_performance_init(DemoWin* self)
{
    if (!self) return;
    XPerformanceOverlay_init(&self->m_performanceOverlay, NULL, 0);
    XPerformanceOverlay_setFontFamily(&self->m_performanceOverlay,
                                      XGUI_DEMO_DEFAULT_FONT_FAMILY);
    XPerformanceOverlay_setTextPixelSize(&self->m_performanceOverlay, 14);
    XPerformanceOverlay_setPresetPosition(
        &self->m_performanceOverlay, XPerformanceOverlayPosition_BottomRight,
        520, 360, 0);
    XPerformanceOverlay_setFixed(&self->m_performanceOverlay, true);
}

/** @brief 绘制性能悬浮层；位置来自控件 geometry。 */
static void demo_performance_draw(DemoWin* self, XPainter* painter)
{
    if (!self || !painter) return;
    XPerformanceOverlay_draw(&self->m_performanceOverlay, painter);
}

static void demo_performance_deinit(DemoWin* self)
{
    if (!self) return;
    XPerformanceOverlay_deinit_base(&self->m_performanceOverlay);
}

/** @brief 判断窗口客户区坐标是否命中性能悬浮层。 */
static bool demo_performance_contains(DemoWin* self, XPoint position)
{
    XRect geo;
    if (!self) return false;
    geo = XPerformanceOverlay_geometry(&self->m_performanceOverlay);
    return position.x >= geo.x && position.x < geo.x + geo.width &&
           position.y >= geo.y && position.y < geo.y + geo.height;
}

#endif /* XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON */

#if XGUI_DEMO_STATIC_SCENE_CACHE_ON
/** @brief 绘制不随性能采样变化的 Demo 场景。 */
static void demo_drawStaticScene(DemoWin* self, XPainter* painter, int w, int h)
{
    XFont painterFont;
    if (!self || !painter || w <= 0 || h <= 0) return;
    XFont_init(&painterFont);
    demo_apply_default_font(&painterFont);
    XPainter_setFont(painter, &painterFont);
    XFont_deinit_base(&painterFont);

    demo_fill_rect(painter, 0, 0, w, h, 0xfff4f6f8u);
    demo_fill_rect(painter, 0, 0, w, 40, 0xff1f4e79u);
    demo_draw_checker(painter, 20, 60, 8, 5, 24);
    demo_fill_rect(painter, 230, 60, 270, 100, 0xffdcefe2u);
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    demo_draw_label(painter, 246, 70, 238, 20,
                    "XLabel 1x \xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95", 16,
                    XGUI_DEMO_DEFAULT_FONT_FAMILY);
    demo_draw_label(painter, 246, 106, 238, 40,
                    "XLabel 32px \xE4\xB8\xAD\xE6\x96\x87", 40,
                    XGUI_DEMO_DEFAULT_FONT_FAMILY);
#else
    XPainter_drawText(painter, 246, 92, "XLabel disabled", 0xff202020u);
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

    demo_fill_rect(painter, 230, 180, 270, 100, 0xffe6eef7u);
#if !XPUSHBUTTON_ON
    XPainter_drawText(painter, 246, 212, "XPushButton disabled", 0xff202020u);
#endif /* !XPUSHBUTTON_ON */

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    demo_fill_rect(painter, 20, 180, 190, 140, 0xffffedcfu);
    XPainter_drawText(painter, 32, 202, "XStackedLayout", 0xff202020u);
    {
        XRect stackRect;
        XRect_init(&stackRect, 32, 220, 166, 48);
        demo_fill_rect(painter, stackRect.x, stackRect.y,
                       stackRect.width, stackRect.height, 0xffffffffu);
        XLayoutItem_setGeometry_base((XLayoutItem*)&self->m_stackLayout,
                                      &stackRect);
    }
#else
    demo_fill_rect(painter, 20, 180, 190, 100, 0xffffedcfu);
    XPainter_drawText(painter, 32, 232, "XStackedLayout disabled", 0xff202020u);
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */
    demo_fill_rect(painter, 0, h - 28, w, 28, 0xff3a3a3au);
}

/** @brief 尺寸或控件状态变化后重建静态场景缓存。 */
static bool demo_updateStaticScene(DemoWin* self, int w, int h)
{
    XPainter painter;
    if (!self || w <= 0 || h <= 0) return false;
    if (!self->m_staticSceneDirty &&
        XImage_width(&self->m_staticScene) == w &&
        XImage_height(&self->m_staticScene) == h)
        return true;
    if (self->m_staticScene.m_data)
        XImage_deinit_base(&self->m_staticScene);
    XImage_init_ex(&self->m_staticScene, w, h, XImageFormat_ARGB32);
    if (XImage_isNull(&self->m_staticScene)) return false;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, &self->m_staticScene)) {
        XPainter_deinit(&painter);
        return false;
    }
    demo_drawStaticScene(self, &painter, w, h);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    self->m_staticSceneDirty = false;
    return true;
}

static bool demo_rectsIntersect(const XRect* first, const XRect* second)
{
    return first && second && first->x < second->x + second->width &&
           second->x < first->x + first->width &&
           first->y < second->y + second->height &&
           second->y < first->y + first->height;
}

/** @brief 将静态场景中对应 tile 的 32 位像素直接复制到后备绘制设备。 */
static bool demo_copyStaticTile(const XImage* scene, XImage* tile,
                                const XRect* tileRect)
{
    const uint8_t* source;
    uint8_t* target;
    size_t rowBytes;
    int row;
    if (!scene || !tile || !tileRect || tileRect->x < 0 || tileRect->y < 0 ||
        tileRect->width <= 0 || tileRect->height <= 0 ||
        tileRect->x + tileRect->width > XImage_width(scene) ||
        tileRect->y + tileRect->height > XImage_height(scene) ||
        tileRect->width > XImage_width(tile) ||
        tileRect->height > XImage_height(tile) || XImage_depth(scene) != 32 ||
        XImage_depth(tile) != 32)
        return false;
    source = XImage_constBits(scene);
    target = XImage_bits(tile);
    if (!source || !target) return false;
    rowBytes = (size_t)tileRect->width * 4u;
    for (row = 0; row < tileRect->height; ++row) {
        memcpy(target + (size_t)row * (size_t)XImage_bytesPerLine(tile),
               source + (size_t)(tileRect->y + row) *
                            (size_t)XImage_bytesPerLine(scene) +
                            (size_t)tileRect->x * 4u,
               rowBytes);
    }
    return true;
}
#endif /* XGUI_DEMO_STATIC_SCENE_CACHE_ON */

/** @brief 重绘整个窗口：resize 后备存储 -> 绘制 -> flush 提交原生窗口。 */
static void demo_paintScene(DemoWin* self, XEvent* event)
{
    XImage* device;
    XPainter painter;
    XPoint offset;
    XRect tile;
    int width;
    int height;
    (void)event;
    if (!self) return;
    width = XWidget_width(&self->m_base);
    height = XWidget_height(&self->m_base);
    if (width <= 0 || height <= 0) return;
    device = XWidget_paintDevice(&self->m_base);
    if (!device) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, device)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(&self->m_base);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    XRect_init(&tile, -offset.x, -offset.y,
               XImage_width(device), XImage_height(device));
#if XGUI_DEMO_STATIC_SCENE_CACHE_ON
    if (!demo_updateStaticScene(self, width, height) ||
        !demo_copyStaticTile(&self->m_staticScene, device, &tile))
        demo_drawStaticScene(self, &painter, width, height);
#else
    demo_drawStaticScene(self, &painter, width, height);
#endif /* XGUI_DEMO_STATIC_SCENE_CACHE_ON */
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    {
        XRect overlay = XPerformanceOverlay_geometry(&self->m_performanceOverlay);
        if (demo_rectsIntersect(&tile, &overlay))
            demo_performance_draw(self, &painter);
    }
#endif /* XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON */
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 按 Qt QWidget::update() 语义合并待绘区域，不同步强制整树重绘。 */
static void demo_repaint(DemoWin* self)
{
    XRect dirty;
    if (!self) return;
#if XGUI_DEMO_STATIC_SCENE_CACHE_ON
    if (self->m_staticSceneDirty ||
        XImage_width(&self->m_staticScene) != XWidget_width(&self->m_base) ||
        XImage_height(&self->m_staticScene) != XWidget_height(&self->m_base)) {
        dirty = XWidget_rect(&self->m_base);
    }
    else
#endif /* XGUI_DEMO_STATIC_SCENE_CACHE_ON */
    {
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
        dirty = XPerformanceOverlay_geometry(&self->m_performanceOverlay);
#else
        dirty = XWidget_rect(&self->m_base);
#endif
    }
    XWidget_updateRect(&self->m_base, &dirty);
}
/** @brief 返回单调微秒计时，用于真实窗口绘制基准。 */
static int64_t demo_monotonicUsecs(void)
{
    /* 统一走 XDateTime 提供的跨平台时钟，避免在 demo 里散落平台时钟 API。 */
    return XDateTime_currentNSecsSinceEpoch() / 1000LL;
}

/**
 * @brief 持续运行真实窗口重绘，统计图形路径的吞吐与最长帧。
 * @details 固定尺寸模式每帧直接完整绘制 demo；尺寸切换模式则交替调用
 *          Win32 SetWindowPos，WM_SIZE 同步进入 resizeEvent 并完成一次
 *          完整绘制。两种模式都经过 XWidget 的后备存储、XPainter 和平台提交。
 */
static void demo_runFrameBenchmark(DemoWin* self, int durationSeconds,
                                   bool resizeWindow)
{
    const int normalWidth = 520;
    const int normalHeight = 360;
    const int largeWidth = 960;
    const int largeHeight = 720;
    int64_t start;
    int64_t now;
    int64_t frameStart;
    int64_t frameUsecs;
    int64_t longestUsecs = 0;
    int64_t elapsedUsecs;
    unsigned frameCount = 0;
    bool large = false;
    if (!self || durationSeconds <= 0)
        return;

    /* 排除窗口首次 show/expose 和首个 DIB 创建成本。 */
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    demo_repaint(self);
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    start = demo_monotonicUsecs();
    do {
        frameStart = demo_monotonicUsecs();
        if (resizeWindow) {
            large = !large;
            XWidget_setGeometry(&self->m_base, 60, 60,
                                large ? largeWidth : normalWidth,
                                large ? largeHeight : normalHeight);
        }
        else {
            demo_repaint(self);
        }
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        frameUsecs = demo_monotonicUsecs() - frameStart;
        if (frameUsecs > longestUsecs)
            longestUsecs = frameUsecs;
        ++frameCount;
        now = demo_monotonicUsecs();
    } while (!self->m_closed &&
             now - start < (int64_t)durationSeconds * 1000000LL);

    elapsedUsecs = now - start;
    if (elapsedUsecs <= 0)
        elapsedUsecs = 1;
    XPrintf("XGuiWindowDemo: benchmark mode=%s frames=%u elapsed=%.3fs "
            "fps=%.1f avg=%.3fms longest=%.3fms\n",
            resizeWindow ? "resize" : "repaint", frameCount,
            (double)elapsedUsecs / 1000000.0,
            (double)frameCount * 1000000.0 / (double)elapsedUsecs,
            (double)elapsedUsecs / (double)frameCount / 1000.0,
            (double)longestUsecs / 1000.0);
}

/** @brief 停止 Demo 自己注册的定时器，避免对象销毁后继续派发刷新事件。 */
static void demo_stopTimers(DemoWin* self)
{
    if (!self) return;
    if (self->m_frameTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_frameTimer);
        self->m_frameTimer = XTIMER_INVALID_ID;
    }
    if (self->m_autoQuitTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_autoQuitTimer);
        self->m_autoQuitTimer = XTIMER_INVALID_ID;
    }
}

/** @brief 标准事件循环中的刷新和自动退出定时器处理。 */
static void VDemoWin_timerEvent(XObject* object, XTimerEvent* event)
{
    DemoWin* self = (DemoWin*)object;
    XTimerId timerId;
    if (!self || !event) return;
    timerId = XTimerEvent_timerId(event);
    if (timerId == self->m_frameTimer) {
        if (!self->m_closed)
            demo_repaint(self);
        XEvent_accept((XEvent*)event);
        return;
    }
    if (timerId == self->m_autoQuitTimer) {
        demo_stopTimers(self);
        self->m_closed = true;
        XGuiApplication_quit();
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void(*)(XObject*, XTimerEvent*))(object, event);
}

/* ==================== 信号槽 ==================== */

#if XWIDGET_ON && XPUSHBUTTON_ON
/** @brief 按钮 pressed 信号槽：置按下状态并更新联动标签。 */
static void demo_button_pressedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    (void)args;
    if (!self) return;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_setText_2(&self->m_linkLabel, "Pressed");
#endif
    self->m_staticSceneDirty = true;
    demo_repaint(self);
}

/** @brief 按钮 released 信号槽：清除按下状态并更新联动标签。 */
static void demo_button_releasedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    (void)args;
    if (!self) return;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_setText_2(&self->m_linkLabel, "Released");
#endif
    self->m_staticSceneDirty = true;
    demo_repaint(self);
}

#if XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
/** @brief 上一页按钮 clicked 槽：循环切换到堆叠布局的上一页。 */
static void demo_stack_prev_clickedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    int index;
    int count;
    (void)args;
    if (!self) return;
    index = XStackedLayout_currentIndex(&self->m_stackLayout);
    count = XStackedLayout_count(&self->m_stackLayout);
    if (count <= 0) return;
    if (index <= 0) index = count - 1;
    else --index;
    XStackedLayout_setCurrentIndex(&self->m_stackLayout, index);
    XPrintf("XGuiWindowDemo: stacked page=%d (prev)\n", index);
    self->m_staticSceneDirty = true;
    demo_repaint(self);
}

/** @brief 下一页按钮 clicked 槽：循环切换到堆叠布局的下一页。 */
static void demo_stack_next_clickedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    int index;
    int count;
    (void)args;
    if (!self) return;
    index = XStackedLayout_currentIndex(&self->m_stackLayout);
    count = XStackedLayout_count(&self->m_stackLayout);
    if (count <= 0) return;
    index = (index + 1) % count;
    XStackedLayout_setCurrentIndex(&self->m_stackLayout, index);
    XPrintf("XGuiWindowDemo: stacked page=%d (next)\n", index);
    self->m_staticSceneDirty = true;
    demo_repaint(self);
}
#endif /* XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */
#endif /* XWIDGET_ON && XPUSHBUTTON_ON */

/* ==================== 事件槽重载 ==================== */

/** @brief CloseEvent：接受关闭，停止刷新并退出应用事件循环。 */
static void VDemoWin_closeEvent(XWidget* self, XEvent* event)
{
    DemoWin* demo = (DemoWin*)self;
    XClass_Parent(XWidget, EXWidget_CloseEvent,
                  void(*)(XWidget*, XEvent*))(self, event);
    demo_stopTimers(demo);
    demo->m_closed = true;
    XGuiApplication_quit();
    if (event) XEvent_accept(event);
}

/** @brief ResizeEvent：更新固定悬浮层锚点，并让控件树提交新尺寸画面。 */
static void VDemoWin_resizeEvent(XWidget* self, XEvent* event)
{
    DemoWin* demo = (DemoWin*)self;
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  void(*)(XWidget*, XEvent*))(self, event);
    demo->m_staticSceneDirty = true;
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    if (XPerformanceOverlay_isFixed(&demo->m_performanceOverlay)) {
        XPerformanceOverlay_setPresetPosition(
            &demo->m_performanceOverlay, XPerformanceOverlayPosition_BottomRight,
            XWidget_width(self), XWidget_height(self), 0);
    }
#endif
    demo_repaint(demo);
}

/** @brief PaintEvent：根控件背景与性能悬浮层；子控件由 XWidget 自动递归绘制。 */
static void VDemoWin_paintEvent(XWidget* self, XEvent* event)
{
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    DemoWin* demo = (DemoWin*)self;
    int64_t frameStartUsecs = demo_monotonicUsecs();
#endif
    demo_paintScene((DemoWin*)self, event);
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    {
        int64_t frameEndUsecs = demo_monotonicUsecs();
        XPerformanceOverlay_updateFrame(&demo->m_performanceOverlay,
                                        frameStartUsecs, frameEndUsecs);
#if XGUI_PERFORMANCE_OVERLAY_NETWORK_ON
        if (demo->m_lastNetworkPollUsecs <= 0 ||
            frameEndUsecs - demo->m_lastNetworkPollUsecs >=
                (int64_t)XGUI_PERFORMANCE_OVERLAY_UPDATE_MS * 1000LL) {
            uint64_t rxBytes = 0;
            uint64_t txBytes = 0;
            bool available = XNetworkStats_readCounters(&rxBytes, &txBytes);
            XPerformanceOverlay_updateNetwork(&demo->m_performanceOverlay,
                                              available, rxBytes, txBytes,
                                              frameEndUsecs);
            demo->m_lastNetworkPollUsecs = frameEndUsecs;
        }
#endif /* XGUI_PERFORMANCE_OVERLAY_NETWORK_ON */
    }
#endif /* XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON */
}

/** @brief KeyPressEvent：打印键码/修饰键/自动重复（真实输入闭环验证）。 */
static void VDemoWin_keyPressEvent(XWidget* self, XEvent* event)
{
    XKeyEvent* key = (XKeyEvent*)event;
    (void)self;
    if (!key) return;
    printf("XGuiWindowDemo: keyPress key=%d modifiers=0x%x autoRepeat=%d\n",
           XKeyEvent_key(key), (unsigned)XKeyEvent_modifiers(key),
           (int)XKeyEvent_autoRepeat(key));
}

/** @brief KeyReleaseEvent：打印释放键码。 */
static void VDemoWin_keyReleaseEvent(XWidget* self, XEvent* event)
{
    XKeyEvent* key = (XKeyEvent*)event;
    (void)self;
    if (!key) return;
    printf("XGuiWindowDemo: keyRelease key=%d modifiers=0x%x\n",
           XKeyEvent_key(key), (unsigned)XKeyEvent_modifiers(key));
}

/** @brief MousePressEvent：背景区域处理性能悬浮层；子控件由自身接收事件。 */
static void VDemoWin_mousePressEvent(XWidget* self, XEvent* event)
{
    DemoWin* demo = (DemoWin*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    if (!mouse) return;
    printf("XGuiWindowDemo: mousePress button=%d buttons=0x%x pos=(%d,%d)\n",
           (int)XMouseEvent_button(mouse), (unsigned)XMouseEvent_buttons(mouse),
           (int)XMouseEvent_position(mouse).x, (int)XMouseEvent_position(mouse).y);
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    {
        XPoint position = XMouseEvent_position(mouse);
        if (demo_performance_contains(demo, position)) {
            if (XMouseEvent_button(mouse) == XMouseButton_RightButton) {
                XPerformanceOverlay_setFixed(
                    &demo->m_performanceOverlay,
                    !XPerformanceOverlay_isFixed(&demo->m_performanceOverlay));
                XPrintf("XGuiWindowDemo: performance overlay fixed=%s\n",
                        XPerformanceOverlay_isFixed(&demo->m_performanceOverlay)
                            ? "true" : "false");
                demo_repaint(demo);
            } else if (XMouseEvent_button(mouse) == XMouseButton_LeftButton) {
                (void)XPerformanceOverlay_beginDrag(
                    &demo->m_performanceOverlay, position.x, position.y);
            }
            XEvent_accept(event);
        }
    }
#endif
}

/** @brief MouseReleaseEvent：结束性能悬浮层拖动。 */
static void VDemoWin_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    DemoWin* demo = (DemoWin*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    if (!mouse) return;
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    if (XMouseEvent_button(mouse) == XMouseButton_LeftButton &&
        XPerformanceOverlay_isDragging(&demo->m_performanceOverlay)) {
        XPerformanceOverlay_endDrag(&demo->m_performanceOverlay);
        demo_repaint(demo);
        XEvent_accept(event);
    }
#endif
}

/** @brief MouseDoubleClickEvent：打印双击键/坐标。 */
static void VDemoWin_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XMouseEvent* mouse = (XMouseEvent*)event;
    (void)self;
    if (!mouse) return;
    printf("XGuiWindowDemo: mouseDoubleClick button=%d pos=(%d,%d)\n",
           (int)XMouseEvent_button(mouse),
           (int)XMouseEvent_position(mouse).x, (int)XMouseEvent_position(mouse).y);
}

/** @brief MouseMoveEvent：拖动性能悬浮层时更新其位置。 */
static void VDemoWin_mouseMoveEvent(XWidget* self, XEvent* event)
{
    DemoWin* demo = (DemoWin*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    if (!mouse) return;
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    if (XPerformanceOverlay_isDragging(&demo->m_performanceOverlay)) {
        XMouseButton buttons = XMouseEvent_buttons(mouse);
        if ((buttons & XMouseButton_LeftButton) != 0) {
            if (XPerformanceOverlay_dragTo(
                    &demo->m_performanceOverlay,
                    XMouseEvent_position(mouse).x,
                    XMouseEvent_position(mouse).y,
                    XWidget_width(self), XWidget_height(self)))
                demo_repaint(demo);
            XEvent_accept(event);
        } else {
            XPerformanceOverlay_endDrag(&demo->m_performanceOverlay);
        }
    }
#endif
}

/** @brief WheelEvent：打印滚轮角度增量与坐标。 */
static void VDemoWin_wheelEvent(XWidget* self, XEvent* event)
{
    XWheelEvent* wheel = (XWheelEvent*)event;
    XPoint delta;
    (void)self;
    if (!wheel) return;
    delta = XWheelEvent_angleDelta(wheel);
    printf("XGuiWindowDemo: wheel delta=(%d,%d) pos=(%d,%d)\n",
           (int)delta.x, (int)delta.y,
           (int)XWheelEvent_position(wheel).x,
           (int)XWheelEvent_position(wheel).y);
}

/** @brief EnterEvent：打印进入坐标（局部+全局）。 */
static void VDemoWin_enterEvent(XWidget* self, XEvent* event)
{
    XEnterEvent* enter = (XEnterEvent*)event;
    XPoint global;
    (void)self;
    if (!enter) return;
    global = XEnterEvent_globalPosition(enter);
    printf("XGuiWindowDemo: enter pos=(%d,%d) global=(%d,%d)\n",
           (int)XEnterEvent_position(enter).x, (int)XEnterEvent_position(enter).y,
           (int)global.x, (int)global.y);
}

/** @brief LeaveEvent：打印离开通知。 */
static void VDemoWin_leaveEvent(XWidget* self, XEvent* event)
{
    (void)self;
    (void)event;
    printf("XGuiWindowDemo: leave\n");
}
/** @brief 演示窗口类虚表初始化。 */
static XVtable* DemoWin_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoWin)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VDemoWin_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent, VDemoWin_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VDemoWin_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VDemoWin_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VDemoWin_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyReleaseEvent, VDemoWin_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VDemoWin_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, VDemoWin_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VDemoWin_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VDemoWin_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VDemoWin_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_EnterEvent, VDemoWin_enterEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent, VDemoWin_leaveEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 创建演示窗口对象并套用子类虚表。 */
static DemoWin* DemoWin_create(void)
{
    DemoWin* self = (DemoWin*)XMemory_malloc(
        sizeof(DemoWin), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(DemoWin));
    XWidget_init(&self->m_base, NULL, 0);
    XClassSetVtable(self, DemoWin);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    XImage_init(&self->m_staticScene);
    self->m_staticSceneDirty = true;
    self->m_frameTimer = XTIMER_INVALID_ID;
    self->m_autoQuitTimer = XTIMER_INVALID_ID;
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    demo_performance_init(self);
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON
    XPushButton_init(&self->m_button, &self->m_base, 0);
    demo_set_widget_default_font((XWidget*)&self->m_button);
    XPushButton_setText_2(&self->m_button, "XPushButton");
    XWidget_setGeometry((XWidget*)&self->m_button, 246, 190, 128, 36);
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_init(&self->m_linkLabel, &self->m_base, 0);
    demo_set_widget_default_font((XWidget*)&self->m_linkLabel);
    XLabel_setText_2(&self->m_linkLabel, "Released");
    XLabel_setTextPixelSize(&self->m_linkLabel, 16);
    XLabel_setAlignment(&self->m_linkLabel, XAlignment_Left | XAlignment_Top);
    XWidget_setGeometry((XWidget*)&self->m_linkLabel, 382, 190, 108, 36);
#endif
    XObject_connect_1((XObject*)&self->m_button,
                      (size_t)XPushButton_pressed_signal(NULL),
                      (XObject*)self, demo_button_pressedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_button,
                      (size_t)XPushButton_released_signal(NULL),
                      (XObject*)self, demo_button_releasedSlot,
                      XConnectionType_Direct);
    /* 与 QWidget 一样，不由布局管理的子控件需显式 show()；父窗口显示
     * 时会把它们转为实际可见。 */
    XWidget_show((XWidget*)&self->m_button);
#if XFRAME_ON && XLABEL_ON
    XWidget_show((XWidget*)&self->m_linkLabel);
#endif
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    XStackedLayout_init(&self->m_stackLayout);
    /* 页面和按钮均是根控件的子控件，由 XWidget 控件树自动绘制与命中。 */
    XLabel_init(&self->m_stackPageOne, &self->m_base, 0);
    demo_set_widget_default_font((XWidget*)&self->m_stackPageOne);
    XLabel_setText_2(&self->m_stackPageOne, "Stacked page 1");
    XLabel_setTextPixelSize(&self->m_stackPageOne, 16);
    XLabel_setAlignment(&self->m_stackPageOne,
                        XAlignment_HCenter | XAlignment_VCenter);
    XLabel_init(&self->m_stackPageTwo, &self->m_base, 0);
    demo_set_widget_default_font((XWidget*)&self->m_stackPageTwo);
    XLabel_setText_2(&self->m_stackPageTwo, "Stacked page 2");
    XLabel_setTextPixelSize(&self->m_stackPageTwo, 16);
    XLabel_setAlignment(&self->m_stackPageTwo,
                        XAlignment_HCenter | XAlignment_VCenter);
    XStackedLayout_addWidget(&self->m_stackLayout,
                             (XWidget*)&self->m_stackPageOne);
    XStackedLayout_addWidget(&self->m_stackLayout,
                             (XWidget*)&self->m_stackPageTwo);
#if XPUSHBUTTON_ON
    XPushButton_init(&self->m_stackPrevButton, &self->m_base, 0);
    demo_set_widget_default_font((XWidget*)&self->m_stackPrevButton);
    XPushButton_setText_2(&self->m_stackPrevButton, "< Prev");
    XWidget_setGeometry((XWidget*)&self->m_stackPrevButton, 32, 274, 78, 30);
    XPushButton_init(&self->m_stackNextButton, &self->m_base, 0);
    demo_set_widget_default_font((XWidget*)&self->m_stackNextButton);
    XPushButton_setText_2(&self->m_stackNextButton, "Next >");
    XWidget_setGeometry((XWidget*)&self->m_stackNextButton, 120, 274, 78, 30);
    XObject_connect_1((XObject*)&self->m_stackPrevButton,
                      (size_t)XPushButton_clicked_signal(NULL, false),
                      (XObject*)self, demo_stack_prev_clickedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_stackNextButton,
                      (size_t)XPushButton_clicked_signal(NULL, false),
                      (XObject*)self, demo_stack_next_clickedSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_stackPrevButton);
    XWidget_show((XWidget*)&self->m_stackNextButton);
#endif /* XPUSHBUTTON_ON */
#endif
    return self;
}

/* ==================== 主函数 ==================== */

int main(int argc, char* argv[])
{
    XGuiApplication* app;
    DemoWin* win;
    int autoSeconds;
    int benchmarkSeconds;
    bool benchmarkResize;
    int argi;
    int eventLoopResult;

    autoSeconds = 0;
    benchmarkSeconds = 0;
    benchmarkResize = false;
    for (argi = 1; argi < argc; ++argi) {
        if (strcmp(argv[argi], "--benchmark") == 0 && argi + 1 < argc) {
            benchmarkSeconds = atoi(argv[++argi]);
            benchmarkResize = false;
        }
        else if (strcmp(argv[argi], "--benchmark-resize") == 0 &&
                 argi + 1 < argc) {
            benchmarkSeconds = atoi(argv[++argi]);
            benchmarkResize = true;
        }
        else {
            autoSeconds = atoi(argv[argi]);
        }
    }
    if (autoSeconds < 0) autoSeconds = 0;
    if (benchmarkSeconds < 0) benchmarkSeconds = 0;

    /* 1) 初始化 XGuiApplication（进程内单例）。 */
    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        XPrintf("XGuiWindowDemo: XGuiApplication_create_ex 失败\n");
        return 1;
    }

    /* 2) 创建演示窗口并设置标题/几何。show() 会在框架内部惰性创建平台窗口。 */
    win = DemoWin_create();
    if (!win) {
        XPrintf("XGuiWindowDemo: DemoWin_create 失败\n");
        XGuiApplication_delete_base(app);
        return 1;
    }
    {
        XString* title = XString_create_utf8(
            "XinYueC GUI 控件可视化测试 (X11/Win32)");
        XWidget_setWindowTitle(&win->m_base, title);
        XString_delete_base((XClass*)title);
    }
    XWidget_setGeometry(&win->m_base, 60, 60, 520, 360);

    /* 3) 显示窗口：触发框架内部的惰性平台窗口创建并进入事件循环。 */
    XWidget_showNormal(&win->m_base);
    XPrintf("XGuiWindowDemo: 屏幕尺寸=%.0fx%.0f\n",
           (double)XWidget_width(&win->m_base),
           (double)XWidget_height(&win->m_base));

    eventLoopResult = 0;
    if (benchmarkSeconds > 0) {
        demo_runFrameBenchmark(win, benchmarkSeconds, benchmarkResize);
    } else {
        win->m_frameTimer = XObject_startTimer_ms(
            (XObject*)win, XGUI_DEMO_FRAME_INTERVAL_MS,
            XTimerType_PreciseTimer);
        if (win->m_frameTimer == XTIMER_INVALID_ID) {
            XPrintf("XGuiWindowDemo: 性能刷新定时器创建失败\n");
            eventLoopResult = 2;
        }
        if (autoSeconds > 0) {
            win->m_autoQuitTimer = XObject_startTimer_ms(
                (XObject*)win, (uint64_t)autoSeconds * 1000u,
                XTimerType_CoarseTimer);
            if (win->m_autoQuitTimer == XTIMER_INVALID_ID) {
                XPrintf("XGuiWindowDemo: 自动退出定时器创建失败\n");
                eventLoopResult = 2;
            }
        }
        if (eventLoopResult == 0)
            eventLoopResult = XGuiApplication_exec();
    }

    XPrintf("XGuiWindowDemo: 退出事件循环（关闭=%s 自动退出=%d 返回=%d）\n",
           win->m_closed ? "是" : "否", autoSeconds, eventLoopResult);

    /* 4) 清理：窗口销毁自动拆除原生窗口；应用单例回收集成层。 */
    demo_stopTimers(win);
#if XWIDGET_ON && XPUSHBUTTON_ON
    XPushButton_deinit_base(&win->m_button);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_deinit_base(&win->m_linkLabel);
#endif
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    demo_performance_deinit(win);
#endif
#if XGUI_DEMO_STATIC_SCENE_CACHE_ON
    XImage_deinit_base(&win->m_staticScene);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
#if XPUSHBUTTON_ON
    XPushButton_deinit_base(&win->m_stackPrevButton);
    XPushButton_deinit_base(&win->m_stackNextButton);
#endif
    XLabel_deinit_base(&win->m_stackPageOne);
    XLabel_deinit_base(&win->m_stackPageTwo);
    XStackedLayout_deinit_base(&win->m_stackLayout);
#endif
    XWidget_delete_base((XClass*)win);
    XGuiApplication_delete_base(app);
    XPrintf("XGuiWindowDemo: 已退出\n");
    return eventLoopResult;
}

#else /* 开关裁剪 */
int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    XPrintf("XGuiWindowDemo: 本演示需要 XGUIAPPLICATION_ON / XWINDOW_ON / "
                    "XBACKINGSTORE_ON / XPLATFORMINTEGRATION_ON / "
                    "XPLATFORMNATIVEWINDOW_ON。\n");
    return 2;
}
#endif /* 开关 */
