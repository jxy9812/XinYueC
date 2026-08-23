/******************************************************************************
 * @file       xgui_window_demo.c
 * @brief      XGui 第一个真实原生窗口演示程序（Linux X11 / Windows Win32）。
 * @details    本程序演示 XGui 完整窗口链路：
 *             - XGuiApplication_create_ex 初始化应用单例；
 *             - 经平台原生接口拿到 XPlatformIntegration；
 *             - XPlatformIntegration_createPlatformWindow 挂接原生窗口
 *               （X11 Window / Win32 HWND，XWindow_createHandle 惰性创建）；
 *             - XBackingStore 离屏缓冲 + XPainter 软件光栅化绘制；
 *             - XWindowSystemInterface 事件注入（Expose/Resize/Close）；
 *             - XGuiApplication_waitForEvents + processEvents 事件泵；
 *             - WM 删除（标题栏 X）触发 CloseEvent -> 接受后关闭退出。
 *             窗口绘制完全走 XImage/XPainter 软件路径，不依赖任何平台
 *             图形 API；平台差异全部隔离在 Drive 后端。
 *             运行：./bin/XGuiWindowDemo_Test [自动退出秒数]
 *                   - 不带参数：常驻，等待窗口标题栏关闭；
 *                   - 带参数：运行指定秒数后自动退出（供无窗口管理器的
 *                     Xvfb/CI 环境截图验收）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CXinYueConfig.h"
#include "XPrintf.h"
#include "XEvent.h"
#include "XDateTime.h"
#include "XGuiApplication.h"
#include "XPlatformNativeInterface.h"
#include "XPlatformIntegration.h"
#include "XWindow.h"
#include "XWindowEvent.h"
#include "XBackingStore.h"
#include "XImage.h"
#include "XPainter.h"

#if XGUIAPPLICATION_ON && XWINDOW_ON && XBACKINGSTORE_ON && \
    XPLATFORMINTEGRATION_ON && XPLATFORMNATIVEWINDOW_ON

/* ==================== 演示窗口子类 ==================== */

XCLASS_DEFINE_BEGING(DemoWin)
XCLASS_DEFINE_EXTEND_END(DemoWin, XWindow)

/** @brief 演示窗口：持有后备存储引用与关闭标志。 */
typedef struct DemoWin
{
    XWindow         m_base;  /**< 基类；必须是第一个成员。 */
    XBackingStore*  m_store; /**< 后备存储借用指针（随窗口生命周期）。 */
    bool            m_closed; /**< CloseEvent 被接受后置真，驱动主循环退出。 */
} DemoWin;

/* ---------------- 绘制 ----------------
 * 全部使用 ARGB32 预乘颜色：软件光栅化 + XBackingStore 上屏，
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

/** @brief 重绘整个窗口：resize 后备存储 -> 绘制 -> flush 提交原生窗口。 */
static void demo_repaint(DemoWin* self)
{
    XBackingStore* store;
    XImage* dev;
    XPainter painter;
    XRegion region;
    XRect rc;
    XPoint offset;
    XSize size;
    int w;
    int h;
    if (!self) return;
    w = XWindow_width((XWindow*)self);
    h = XWindow_height((XWindow*)self);
    if (w <= 0 || h <= 0) return;
    store = self->m_store;
    if (!store) return;

    /* 1) 按窗口客户区尺寸调整后备缓冲（尺寸未变时内部为 no-op）。 */
    XSize_init(&size, w, h);
    XBackingStore_resize(store, &size);

    /* 2) 进入绘制区间，把 XPainter 绑定到后备缓冲的 XImage 上。 */
    XRegion_init(&region);
    XRect_init(&rc, 0, 0, w, h);
    XRegion_addRect(&region, &rc);
    XBackingStore_beginPaint(store, &region);
    XPainter_init(&painter, NULL);
    dev = XBackingStore_paintDevice(store);
    if (dev && XPainter_begin_image(&painter, dev)) {
        /* 背景。 */
        demo_fill_rect(&painter, 0, 0, w, h, 0xfff4f6f8u);
        /* 顶部标题栏条。 */
        demo_fill_rect(&painter, 0, 0, w, 40, 0xff1f4e79u);
        /* 中部棋盘格纹理。 */
        demo_draw_checker(&painter, 20, 60, 8, 5, 24);
        /* 左侧信息面板。 */
        demo_fill_rect(&painter, 230, 60, 190, 40, 0xff2e8b57u);
        /* 状态栏。 */
        demo_fill_rect(&painter, 0, h - 28, w, 28, 0xff3a3a3au);
        XPainter_end(&painter);
    }
    XBackingStore_endPaint(store);

    /* 3) 把整窗脏区提交到原生窗口（X11 XPutImage / Win32 BitBlt）。 */
    XPoint_init(&offset, 0, 0);
    XBackingStore_flush(store, &region, (XWindow*)self, &offset);
    XRegion_deinit(&region);
}

/* ==================== 事件槽重载 ==================== */

/** @brief CloseEvent：接受关闭并置标志，主循环随后退出。 */
static void VDemoWin_closeEvent(XWindow* self, XEvent* event)
{
    /* 注意：XWindow_xxxEvent_base 在框架里是虚派发（会再次进入本覆写），
       必须用 XClass_Parent 显式调用父类默认实现，避免无限递归。 */
    XClass_Parent(XWindow, EXWindow_CloseEvent, XWindowEventSlot)(self, event);
    ((DemoWin*)self)->m_closed = true;
    if (event) XEvent_accept(event);
}

/** @brief ExposeEvent：区域可见，整窗重绘。 */
static void VDemoWin_exposeEvent(XWindow* self, XEvent* event)
{
    XClass_Parent(XWindow, EXWindow_ExposeEvent, XWindowEventSlot)(self, event);
    demo_repaint((DemoWin*)self);
}

/** @brief ResizeEvent：尺寸变化，整窗重绘。 */
static void VDemoWin_resizeEvent(XWindow* self, XEvent* event)
{
    XClass_Parent(XWindow, EXWindow_ResizeEvent, XWindowEventSlot)(self, event);
    demo_repaint((DemoWin*)self);
}

/** @brief PaintEvent：显式绘制请求（本演示与 Expose 同路整窗刷新）。 */
static void VDemoWin_paintEvent(XWindow* self, XEvent* event)
{
    XClass_Parent(XWindow, EXWindow_PaintEvent, XWindowEventSlot)(self, event);
    demo_repaint((DemoWin*)self);
}

/** @brief KeyPressEvent：打印键码/修饰键/自动重复（真实输入闭环验证）。 */
static void VDemoWin_keyPressEvent(XWindow* self, XEvent* event)
{
    XKeyEvent* ke;
    XClass_Parent(XWindow, EXWindow_KeyPressEvent, XWindowEventSlot)(self, event);
    ke = (XKeyEvent*)event;
    printf("XGuiWindowDemo: keyPress key=%d modifiers=0x%x autoRepeat=%d\n",
           XKeyEvent_key(ke), (unsigned)XKeyEvent_modifiers(ke),
           (int)XKeyEvent_autoRepeat(ke));
}

/** @brief KeyReleaseEvent：打印释放键码。 */
static void VDemoWin_keyReleaseEvent(XWindow* self, XEvent* event)
{
    XKeyEvent* ke;
    XClass_Parent(XWindow, EXWindow_KeyReleaseEvent, XWindowEventSlot)(self, event);
    ke = (XKeyEvent*)event;
    printf("XGuiWindowDemo: keyRelease key=%d modifiers=0x%x\n",
           XKeyEvent_key(ke), (unsigned)XKeyEvent_modifiers(ke));
}

/** @brief MousePressEvent：打印触发键/按下集合/坐标。 */
static void VDemoWin_mousePressEvent(XWindow* self, XEvent* event)
{
    XMouseEvent* me;
    XClass_Parent(XWindow, EXWindow_MousePressEvent, XWindowEventSlot)(self, event);
    me = (XMouseEvent*)event;
    printf("XGuiWindowDemo: mousePress button=%d buttons=0x%x pos=(%d,%d)\n",
           (int)XMouseEvent_button(me), (unsigned)XMouseEvent_buttons(me),
           (int)XMouseEvent_position(me).x, (int)XMouseEvent_position(me).y);
}

/** @brief MouseReleaseEvent：打印释放键/坐标。 */
static void VDemoWin_mouseReleaseEvent(XWindow* self, XEvent* event)
{
    XMouseEvent* me;
    XClass_Parent(XWindow, EXWindow_MouseReleaseEvent, XWindowEventSlot)(self, event);
    me = (XMouseEvent*)event;
    printf("XGuiWindowDemo: mouseRelease button=%d buttons=0x%x pos=(%d,%d)\n",
           (int)XMouseEvent_button(me), (unsigned)XMouseEvent_buttons(me),
           (int)XMouseEvent_position(me).x, (int)XMouseEvent_position(me).y);
}

/** @brief MouseDoubleClickEvent：打印双击键/坐标。 */
static void VDemoWin_mouseDoubleClickEvent(XWindow* self, XEvent* event)
{
    XMouseEvent* me;
    XClass_Parent(XWindow, EXWindow_MouseDoubleClickEvent, XWindowEventSlot)(self, event);
    me = (XMouseEvent*)event;
    printf("XGuiWindowDemo: mouseDoubleClick button=%d pos=(%d,%d)\n",
           (int)XMouseEvent_button(me),
           (int)XMouseEvent_position(me).x, (int)XMouseEvent_position(me).y);
}

/** @brief MouseMoveEvent：打印坐标与按下集合。 */
static void VDemoWin_mouseMoveEvent(XWindow* self, XEvent* event)
{
    XMouseEvent* me;
    XClass_Parent(XWindow, EXWindow_MouseMoveEvent, XWindowEventSlot)(self, event);
    me = (XMouseEvent*)event;
    printf("XGuiWindowDemo: mouseMove buttons=0x%x pos=(%d,%d)\n",
           (unsigned)XMouseEvent_buttons(me),
           (int)XMouseEvent_position(me).x, (int)XMouseEvent_position(me).y);
}

/** @brief WheelEvent：打印滚轮角度增量与坐标。 */
static void VDemoWin_wheelEvent(XWindow* self, XEvent* event)
{
    XWheelEvent* we;
    XPoint delta;
    XClass_Parent(XWindow, EXWindow_WheelEvent, XWindowEventSlot)(self, event);
    we = (XWheelEvent*)event;
    delta = XWheelEvent_angleDelta(we);
    printf("XGuiWindowDemo: wheel delta=(%d,%d) pos=(%d,%d)\n",
           (int)delta.x, (int)delta.y,
           (int)XWheelEvent_position(we).x, (int)XWheelEvent_position(we).y);
}

/** @brief EnterEvent：打印进入坐标（局部+全局）。 */
static void VDemoWin_enterEvent(XWindow* self, XEvent* event)
{
    XEnterEvent* ee;
    XPoint global;
    XClass_Parent(XWindow, EXWindow_EnterEvent, XWindowEventSlot)(self, event);
    ee = (XEnterEvent*)event;
    global = XEnterEvent_globalPosition(ee);
    printf("XGuiWindowDemo: enter pos=(%d,%d) global=(%d,%d)\n",
           (int)XEnterEvent_position(ee).x, (int)XEnterEvent_position(ee).y,
           (int)global.x, (int)global.y);
}

/** @brief LeaveEvent：打印离开通知。 */
static void VDemoWin_leaveEvent(XWindow* self, XEvent* event)
{
    XClass_Parent(XWindow, EXWindow_LeaveEvent, XWindowEventSlot)(self, event);
    printf("XGuiWindowDemo: leave\n");
}

/** @brief 演示窗口类虚表初始化。 */
static XVtable* DemoWin_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoWin)
    XVTABLE_INHERIT_XCLASS(XWindow);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_CloseEvent, VDemoWin_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_ExposeEvent, VDemoWin_exposeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_ResizeEvent, VDemoWin_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_PaintEvent, VDemoWin_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_KeyPressEvent, VDemoWin_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_KeyReleaseEvent, VDemoWin_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MousePressEvent, VDemoWin_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MouseReleaseEvent, VDemoWin_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MouseDoubleClickEvent,
                             VDemoWin_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MouseMoveEvent, VDemoWin_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_WheelEvent, VDemoWin_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_EnterEvent, VDemoWin_enterEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_LeaveEvent, VDemoWin_leaveEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 创建演示窗口对象并套用子类虚表。 */
static DemoWin* DemoWin_create(void)
{
    DemoWin* self = (DemoWin*)XMemory_malloc(
        sizeof(DemoWin), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(DemoWin));
    XWindow_init(&self->m_base);
    XClassSetVtable(self, DemoWin);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 主函数 ==================== */

int main(int argc, char* argv[])
{
    XGuiApplication* app;
    XPlatformNativeInterface* gni;
    XPlatformIntegration* gpi;
    XPlatformWindow* pw;
    DemoWin* win;
    XBackingStore* store;
    int autoSeconds;
    int64_t startMsec;
    bool ok;

    autoSeconds = (argc > 1) ? atoi(argv[1]) : 0;
    if (autoSeconds < 0) autoSeconds = 0;

    /* 1) 初始化 XGuiApplication（进程内单例）。 */
    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        XPrintf("XGuiWindowDemo: XGuiApplication_create_ex 失败\n");
        return 1;
    }

    /* 2) 创建演示窗口并设置标题/几何（原生窗口在首次显示时惰性创建）。 */
    win = DemoWin_create();
    if (!win) {
        XPrintf("XGuiWindowDemo: DemoWin_create 失败\n");
        XGuiApplication_delete_base(app);
        return 1;
    }
    XWindow_setTitle_2((XWindow*)win, "XinYueC 原生窗口演示 (X11/Win32)");
    XWindow_setGeometry((XWindow*)win, 60, 60, 520, 360);

    /* 3) 挂接平台原生接口与平台窗口句柄。 */
    gni = XGuiApplication_platformNativeInterface();
    gpi = (gni) ? XPlatformNativeInterface_integration(gni) : NULL;
    if (!gni || !gpi) {
        XPrintf("XGuiWindowDemo: 平台原生接口不可用（XPLATFORMNATIVEWINDOW_ON?）\n");
        XWindow_delete_base((XClass*)win);
        XGuiApplication_delete_base(app);
        return 1;
    }
    pw = XPlatformIntegration_createPlatformWindow(gpi, (XWindow*)win);
    if (!pw) {
        XPrintf("XGuiWindowDemo: createPlatformWindow 失败\n");
        XWindow_delete_base((XClass*)win);
        XGuiApplication_delete_base(app);
        return 1;
    }

    /* 4) 创建后备存储：paintDevice 为 XImage 软件缓冲，随后 XPainter 直绘。 */
    store = XBackingStore_create((XWindow*)win);
    if (!store) {
        XPrintf("XGuiWindowDemo: XBackingStore_create 失败\n");
        XWindow_delete_base((XClass*)win);
        XGuiApplication_delete_base(app);
        return 1;
    }
    win->m_store = store;

    /* 5) 显示窗口：触发惰性创建原生窗口并进入事件循环。 */
    XWindow_showNormal((XWindow*)win);
    startMsec = XDateTime_currentMSecsSinceEpoch();
    ok = XPlatformIntegration_isNativeWindowAvailable(gpi);
    XPrintf("XGuiWindowDemo: 原生窗口可用=%s 屏幕尺寸=%.0fx%.0f\n",
           ok ? "是" : "否",
           (double)XWindow_width((XWindow*)win),
           (double)XWindow_height((XWindow*)win));

    while (!win->m_closed) {
        int64_t nowMsec;
        if (autoSeconds > 0) {
            nowMsec = XDateTime_currentMSecsSinceEpoch();
            if ((nowMsec - startMsec) / 1000 >= (int64_t)autoSeconds) break;
        }
        XGuiApplication_waitForEvents(50); /* 平台消息泵（X11/Win32）+ 队列等待。 */
        XGuiApplication_processEvents(XEventLoop_AllEvents);
    }

    XPrintf("XGuiWindowDemo: 退出事件循环（关闭=%s 自动退出=%d）\n",
           win->m_closed ? "是" : "否", autoSeconds);

    /* 6) 清理：窗口销毁自动拆除原生窗口；应用单例回收集成层。 */
    XBackingStore_delete_base((XClass*)store);
    win->m_store = NULL;
    XWindow_delete_base((XClass*)win);
    XGuiApplication_delete_base(app);
    XPrintf("XGuiWindowDemo: 已退出\n");
    return 0;
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
