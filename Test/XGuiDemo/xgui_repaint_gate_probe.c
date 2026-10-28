/* [XGUI_REPAINT_MAX_FPS 闸取证探针 2026-10-27]
 * 整文件受本宏守卫：常规构建（含 XGuiWindowDemo_Test 的 GLOB 收集）编译为
 * 空 TU（仅保留 typedef 防 ISO C 空翻译单元告警），只有探针手动编译线
 * （-DXGUI_REPAINT_GATE_PROBE_STANDALONE，链接 libXinYueCS.a 独立出 main）
 * 才展开，见文件尾编译命令注释。
 *
 * 目的：在桌面 Xvfb 上复刻「触摸拖动 → 合成鼠标 MOVE → 控件 update/move →
 * 重绘节奏闸 → flush → paintTree」整链，量化三个口径：
 *   topPaint/s   = 顶层 paintEvent 派发次数（HUD FPS 的同口径计数，
 *                  xgui_window_demo.c VDemoWin_paintEvent 每 PAINT 计 1 帧）
 *   flush/s      = XWidget_flushBackingStore 入口次数（g_flushBsCount 差分）
 *   post/s       = PAINT 投递次数（g_postPaintCount 差分，闸决策点）
 *   maxRects     = 单次顶层 PAINT 事件携带的最大矩形数（paintTree 多矩形
 *                  逐矩形重派 paintEvent 的放大倍数上界，XWidget.c:9056）
 * 结论读法：topPaint/s ≈ flush/s ⇒ 闸有效且口径一致；topPaint/s ≈
 * N×flush/s ⇒ 闸有效但 HUD 口径被多矩形拆分放大；flush/s ≫ 1000/fps ⇒
 * 存在绕闸 flush 路径。
 *
 * 手动编译命令（仓库根执行）：
 *   gcc -O2 -DXGUI_REPAINT_GATE_PROBE_STANDALONE -o /tmp/wfA-probe \
 *       Test/XGuiDemo/xgui_repaint_gate_probe.c \
 *       $(find Src -type d | sed 's/^/-I/' | tr '\n' ' ') -ISrc/XPlatform \
 *       /tmp/wfA-build/libXinYueCS.a /tmp/wfA-build/libmbedtls.a \
 *       /tmp/wfA-build/liblwip.a /tmp/wfA-build/libzlib.a \
 *       /tmp/wfA-build/libfatfs.a /tmp/wfA-build/libpcre2.a \
 *       -lm -lpthread -ldl -lX11
 * 运行：
 *   XGUI_REPAINT_MAX_FPS=60 xvfb-run -a -s "-screen 0 800x480x24" /tmp/wfA-probe
 */
#ifdef XGUI_REPAINT_GATE_PROBE_STANDALONE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "XGuiApplication.h"
#include "XWidget.h"
#include "XWidget_Protected.h" /* paintImage/paintOffset（离屏绘制入口）。 */
#include "XWindow.h"
#include "XWindowSystemInterface.h"
#include "XWindowEvent.h"
#include "XTimer.h"
#include "XMemory.h"
#include "XString.h"
#include "XPainter.h"
#include "XDateTime.h"

/* 与 XWidget.c 同款插桩计数器（框架全局，非 static）。 */
extern volatile unsigned long g_flushBsCount;
extern volatile unsigned long g_postPaintCount;
extern volatile unsigned long g_exposeEvtCount;

/** @brief 探针顶层（m_base 必须为首成员）。 */
typedef struct ProbeWin
{
    XWidget m_base;        /**< 基类；必须是第一个成员。 */
    XWidget* m_panel;      /**< 被拖动子件（HUD 悬浮层替身）。 */
    XWidget* m_cornerA;    /**< 远角子件A（多矩形脏区制造，左上）。 */
    XWidget* m_cornerB;    /**< 远角子件B（多矩形脏区制造，右下）。 */
    int m_multiRect;       /**< 1=每拍同时标脏远角两子件（多矩形模式）。 */
    int m_paintCount;      /**< 本顶层 paintEvent 派发计数（HUD 口径）。 */
    int m_childPaintCount; /**< 面板子件 paintEvent 计数（单遍内 N 的构成）。 */
    int m_maxRects;        /**< 单次 PAINT 事件携带的最大矩形数。 */
    int m_dragging;        /**< 注入流处于按住拖动态。 */
} ProbeWin;

static ProbeWin* g_win = NULL;

XCLASS_DEFINE_BEGING(ProbeWin)
XCLASS_DEFINE_EXTEND_END(ProbeWin, XWidget)

static void probe_panel_paintEvent(XWidget* self, XEvent* event)
{
    (void)self;
    if (!event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    if (g_win) ++g_win->m_childPaintCount;
}

XCLASS_DEFINE_BEGING(ProbePanel)
XCLASS_DEFINE_EXTEND_END(ProbePanel, XWidget)

XVtable* ProbePanel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(ProbePanel)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, probe_panel_paintEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 顶层绘制：整幅底色（成本近似 demo_paintScene 的整窗基底）。 */
static void VProbeWin_paintEvent(XWidget* self, XEvent* event)
{
    ProbeWin* win = (ProbeWin*)self;
    XPainter painter;
    XImage* image;
    XRect rect;
    XPoint offset;
    XRegion region;
    if (!event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    ++win->m_paintCount;
    region = XPaintEvent_region((const XPaintEvent*)event);
    if (region.count > win->m_maxRects)
        win->m_maxRects = region.count;
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    XRect_init(&rect, 0, 0, XWidget_width(self), XWidget_height(self));
    XPainter_fillRect(&painter, &rect, 0xff202830u);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 拖动跟随：面板子件随左键 MOVE 平移（HUD dragTo→XWidget_move
 *         同构；XWidget_move 内部经 recomputeGeometry 走闸内 update）。 */
static void VProbeWin_mouseMoveEvent(XWidget* self, XEvent* event)
{
    ProbeWin* win = (ProbeWin*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    if (!mouse) return;
    if (win->m_dragging &&
        (XMouseEvent_buttons(mouse) & XMouseButton_LeftButton) != 0) {
        XPoint pos = XMouseEvent_position(mouse);
        XWidget_move(win->m_panel, pos.x - 70, pos.y - 24);
        if (win->m_multiRect) {
            /* 模拟滚动/列表拖动的多点失效：远角两子件逐拍标脏（矩形
               互不相交也不相邻，addRect 不可合并 ⇒ 顶层脏区多矩形）。 */
            XWidget_update(win->m_cornerA);
            XWidget_update(win->m_cornerB);
        }
        XEvent_accept(event);
    }
}

static void VProbeWin_mousePressEvent(XWidget* self, XEvent* event)
{
    ProbeWin* win = (ProbeWin*)self;
    (void)event;
    win->m_dragging = 1;
}

static void VProbeWin_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    ProbeWin* win = (ProbeWin*)self;
    (void)event;
    win->m_dragging = 0;
}

XVtable* ProbeWin_class_init(void)
{
    XVTABLE_INIT_DEFAULT(ProbeWin)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VProbeWin_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VProbeWin_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VProbeWin_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VProbeWin_mouseReleaseEvent);
    return XVTABLE_DEFAULT;
}

/* ---- 注入流状态 ---- */
static XTimer* g_injectTimer = NULL;
static int g_tick = 0;
static int g_posX = 40;
static int g_posY = 160;
static int64_t g_lastStamp = 0;

/** @brief 1s 统计窗打印（与 HUD 250ms 窗同思路，粒度放宽求稳）。 */
static void probe_report(int64_t nowMs)
{
    static unsigned long lastFlush = 0;
    static unsigned long lastPost = 0;
    static unsigned long lastExpose = 0;
    static int lastTopPaint = 0;
    static int lastChildPaint = 0;
    static int64_t lastMs = 0;
    static int win = 0;
    double dt;
    if (lastMs == 0) {
        lastMs = nowMs;
        lastFlush = g_flushBsCount;
        lastPost = g_postPaintCount;
        lastExpose = g_exposeEvtCount;
        lastTopPaint = g_win->m_paintCount;
        lastChildPaint = g_win->m_childPaintCount;
        return;
    }
    dt = (double)(nowMs - lastMs) / 1000.0;
    if (dt < 0.5) return;
    ++win;
    printf("[probe] w%d %.1fs: flush=%.0f/s post=%.0f/s expose=%.0f/s "
           "topPaint=%.0f/s childPaint=%.0f/s maxRects=%d\n",
           win, dt,
           (double)(g_flushBsCount - lastFlush) / dt,
           (double)(g_postPaintCount - lastPost) / dt,
           (double)(g_exposeEvtCount - lastExpose) / dt,
           (double)(g_win->m_paintCount - lastTopPaint) / dt,
           (double)(g_win->m_childPaintCount - lastChildPaint) / dt,
           g_win->m_maxRects);
    fflush(stdout);
    lastMs = nowMs;
    lastFlush = g_flushBsCount;
    lastPost = g_postPaintCount;
    lastExpose = g_exposeEvtCount;
    lastTopPaint = g_win->m_paintCount;
    lastChildPaint = g_win->m_childPaintCount;
}

/** @brief 4ms 注入定时器：250Hz 左键按住 MOVE（触摸报点同量级），
 *         10s 后抬升并退出。 */
static void onInjectTick(void* userData, XTimerData* timer)
{
    XWindow* xwin;
    XPoint local;
    XPoint global;
    int64_t nowMs = XDateTime_currentMSecsSinceEpoch();
    (void)userData; (void)timer;
    if (!g_win) return;
    xwin = XWidget_windowHandle(&g_win->m_base);
    if (!xwin) return;
    if (g_tick == 0) {
        /* 首拍：注入 PRESS（拖动序列起点）。 */
        XPoint_init(&local, g_posX, g_posY);
        XPoint_init(&global, g_posX, g_posY);
        XWindowSystemInterface_handleMouseEvent_ex(
            xwin, XEVENT_TYPE_MOUSE_BUTTON_PRESS, XMouseButton_LeftButton,
            XMouseButton_LeftButton, XKeyboardModifier_NoModifier, local,
            &global, nowMs);
        g_lastStamp = nowMs;
    } else if (g_tick <= 40 * 25) { /* 25s 名义、约 4~5s 实际拖动段 */
        XPoint_init(&local, g_posX, g_posY);
        XPoint_init(&global, g_posX, g_posY);
        XWindowSystemInterface_handleMouseEvent_ex(
            xwin, XEVENT_TYPE_MOUSE_MOVE, XMouseButton_NoButton,
            XMouseButton_LeftButton, XKeyboardModifier_NoModifier, local,
            &global, nowMs);
        probe_report(nowMs);
        /* 横向往复拖动：面板随指平移（脏区=old∪new 横带）。 */
        g_posX += 6;
        if (g_posX > 620) g_posX = 40;
    } else {
        static int ended = 0;
        if (!ended) {
            XPoint_init(&local, g_posX, g_posY);
            XPoint_init(&global, g_posX, g_posY);
            XWindowSystemInterface_handleMouseEvent_ex(
                xwin, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                XMouseButton_LeftButton, XMouseButton_NoButton,
                XKeyboardModifier_NoModifier, local, &global, nowMs);
            ended = 1;
            printf("[probe] done: total topPaint=%d childPaint=%d "
                   "flush=%lu post=%lu expose=%lu maxRects=%d\n",
                   g_win->m_paintCount, g_win->m_childPaintCount,
                   g_flushBsCount, g_postPaintCount, g_exposeEvtCount,
                   g_win->m_maxRects);
            fflush(stdout);
            XGuiApplication_quit();
        }
    }
    ++g_tick;
}

int main(int argc, char* argv[])
{
    XTimer* reportTick;
    (void)argc; (void)argv;
    if (!XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL)) {
        printf("probe: app create failed\n");
        return 1;
    }
    g_win = (ProbeWin*)XMemory_malloc(sizeof(ProbeWin),
                                      XCLASS_DEFAULT_MEMORY_TYPE);
    if (!g_win) return 1;
    XMemset(g_win, 0, sizeof(*g_win));
    XWidget_init(&g_win->m_base, NULL, (XWidgetFlags)XWindowType_Window);
    XClassSetVtable(g_win, ProbeWin);
    XWidget_resize(&g_win->m_base, 700, 320);
    XWidget_setWindowTitle(&g_win->m_base, XString_create_utf8("闸取证探针"));
    g_win->m_panel = (XWidget*)XMemory_malloc(sizeof(XWidget),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
    if (!g_win->m_panel) return 1;
    XMemset(g_win->m_panel, 0, sizeof(*g_win->m_panel));
    XWidget_init(g_win->m_panel, &g_win->m_base, 0);
    XClassSetVtable(g_win->m_panel, ProbePanel);
    XWidget_setGeometry(g_win->m_panel, 40, 136, 140, 48);
    /* 多矩形模式：argv[1]=multi。远角两子件由 MOVE 拍逐拍标脏。 */
    if (argc > 1 && strcmp(argv[1], "multi") == 0)
        g_win->m_multiRect = 1;
    if (g_win->m_multiRect) {
        g_win->m_cornerA = (XWidget*)XMemory_malloc(sizeof(XWidget),
                                                    XCLASS_DEFAULT_MEMORY_TYPE);
        g_win->m_cornerB = (XWidget*)XMemory_malloc(sizeof(XWidget),
                                                    XCLASS_DEFAULT_MEMORY_TYPE);
        if (g_win->m_cornerA) {
            XMemset(g_win->m_cornerA, 0, sizeof(*g_win->m_cornerA));
            XWidget_init(g_win->m_cornerA, &g_win->m_base, 0);
            XClassSetVtable(g_win->m_cornerA, ProbePanel);
            XWidget_setGeometry(g_win->m_cornerA, 8, 8, 60, 20);
        }
        if (g_win->m_cornerB) {
            XMemset(g_win->m_cornerB, 0, sizeof(*g_win->m_cornerB));
            XWidget_init(g_win->m_cornerB, &g_win->m_base, 0);
            XClassSetVtable(g_win->m_cornerB, ProbePanel);
            XWidget_setGeometry(g_win->m_cornerB, 620, 280, 60, 20);
        }
    }
    XWidget_show(&g_win->m_base);

    /* 注入定时器须在事件循环起来后首拍即注入 PRESS：XTimer 到期经分发器，
       窗口 show 后句柄已建，可直接注入。 */
    g_injectTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XTimer_setInterval(g_injectTimer, 4);
    XTimer_setTimerCallback(g_injectTimer, onInjectTick);
    XTimer_start_base(g_injectTimer);
    reportTick = NULL; /* 统计走注入拍内 probe_report，不另起定时器。 */

    XGuiApplication_exec();
    printf("[probe] exit\n");
    return 0;
}

#else /* !XGUI_REPAINT_GATE_PROBE_STANDALONE */
/* 常规构建（demo GLOB 收集本文件）：空 TU 占位。 */
typedef int xgui_repaint_gate_probe_placeholder_t;
#endif /* XGUI_REPAINT_GATE_PROBE_STANDALONE */
